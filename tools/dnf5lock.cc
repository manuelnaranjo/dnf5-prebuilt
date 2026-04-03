/*

Resolve the full transitive dependency closure for a set of RPM packages per
target and emit a lock file with repo-relative paths, SHA256 checksums, sizes,
and a resolved dependency graph.

Copyright (C) 2026  Manuel Naranjo

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/conf/vars.hpp>
#include <libdnf5/repo/repo.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package.hpp>
#include <libdnf5/common/sack/query_cmp.hpp>
#include <libdnf5/rpm/package_query.hpp>

#include <json.h>
#include <popt.h>
#include <fnmatch.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


// ── Option identifiers for repeatable popt flags ─────────────────────────────

enum {
    OPT_ARCH      = 1,
    OPT_REPODIR   = 2,
    OPT_CONFIG    = 3,
    OPT_REPO      = 4,
    OPT_EXCLUDE   = 5,
    OPT_STRIP     = 6,
    OPT_GPGKEYDIR = 7,
    OPT_VAR       = 8,
};

static const char * opt_output   = nullptr;
static int          opt_gpgcheck = 0;

static struct poptOption options_table[] = {
    { "output", 'o', POPT_ARG_STRING, &opt_output, 0,
      "write lock file to FILE (default: stdout)", "FILE" },
    { "arch", '\0', POPT_ARG_STRING, nullptr, OPT_ARCH,
      "target architecture, required, repeatable", "ARCH" },
    { "repodir", '\0', POPT_ARG_STRING, nullptr, OPT_REPODIR,
      "use .repo files from DIR instead of system config (repeatable)", "DIR" },
    { "config", '\0', POPT_ARG_STRING, nullptr, OPT_CONFIG,
      "dnf configuration file", "FILE" },
    { "repo", '\0', POPT_ARG_STRING, nullptr, OPT_REPO,
      "enable only repo ID (repeatable; currently warns, not enforced)", "ID" },
    { "exclude", '\0', POPT_ARG_STRING, nullptr, OPT_EXCLUDE,
      "pre-solver: exclude packages matching PATTERN (glob, repeatable)", "PATTERN" },
    { "strip", '\0', POPT_ARG_STRING, nullptr, OPT_STRIP,
      "post-solver: remove matching packages from output (glob, repeatable)", "PATTERN" },
    { "gpg-check", '\0', POPT_ARG_VAL, &opt_gpgcheck, 1,
      "verify GPG signatures of all resolved packages", nullptr },
    { "gpgkeydir", '\0', POPT_ARG_STRING, nullptr, OPT_GPGKEYDIR,
      "directory containing trusted GPG key files", "DIR" },
    { "var", '\0', POPT_ARG_STRING, nullptr, OPT_VAR,
      "set DNF variable NAME=VALUE at runtime priority (repeatable)", "NAME=VALUE" },
    POPT_AUTOHELP
    POPT_TABLEEND
};


// ── Data structures ───────────────────────────────────────────────────────────

struct PkgInfo {
    std::string name;                        // for strip glob matching
    std::string repo_id;
    std::string location;                    // repo-relative path
    std::string sha256;                      // hex
    int64_t     size = 0;
    std::vector<std::string> provides_names; // from get_provides(), for dep resolution
    std::vector<std::string> requires_names; // from get_requires(), for dep resolution
    std::set<std::string>    deps;           // resolved dep NEVRAs, computed by compute_deps()
};

struct LockData {
    std::vector<std::string> arches;
    std::map<std::string, std::string>   repos;     // repo_id -> baseurl (sorted)
    std::map<std::string, PkgInfo>       packages;  // nevra -> PkgInfo (sorted)

    // target -> arch -> flat list of closure NEVRAs
    std::map<std::string, std::map<std::string, std::vector<std::string>>> closures;
    // target -> arch -> error string (resolution or GPG failure)
    std::map<std::string, std::map<std::string, std::string>> errors;
};


// ── Helpers ───────────────────────────────────────────────────────────────────

static bool glob_match(const std::vector<std::string> & patterns,
                       const std::string & name)
{
    for (const auto & p : patterns)
        if (fnmatch(p.c_str(), name.c_str(), 0) == 0) return true;
    return false;
}

// Resolve package-level deps (NEVRA references) from raw provides/requires names.
// Builds a provides-name → NEVRA index across all collected packages, then
// maps each package's requires to the NEVRA that provides the required name.
static void compute_deps(LockData & lock)
{
    std::unordered_map<std::string, std::string> provides_idx;
    for (auto & [nevra, info] : lock.packages)
        for (const auto & pname : info.provides_names)
            provides_idx.emplace(pname, nevra);  // first-in-wins on collision

    for (auto & [nevra, info] : lock.packages)
        for (const auto & rname : info.requires_names) {
            auto it = provides_idx.find(rname);
            if (it != provides_idx.end() && it->second != nevra)
                info.deps.insert(it->second);
        }
}

// Remove packages matching strip_patterns, then iteratively prune orphaned
// transitive deps (packages whose every reverse-dep is already stripped).
// Leaves deps fields of remaining packages intact even if they reference
// stripped NEVRAs, preserving full dependency information.
static void apply_strip(LockData & lock,
                        const std::vector<std::string> & strip_patterns)
{
    // Build reverse-dep index: dep_nevra -> {NEVRAs that depend on it}
    std::unordered_map<std::string, std::unordered_set<std::string>> rdeps;
    for (auto & [nevra, info] : lock.packages)
        for (const auto & dep : info.deps)
            rdeps[dep].insert(nevra);

    // Seed with directly-matched packages.
    std::unordered_set<std::string> stripped;
    for (auto & [nevra, info] : lock.packages)
        if (glob_match(strip_patterns, info.name))
            stripped.insert(nevra);

    // Iteratively pull in packages whose every reverse-dep is already stripped.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto & [nevra, _] : lock.packages) {
            if (stripped.count(nevra)) continue;
            auto it = rdeps.find(nevra);
            if (it == rdeps.end() || it->second.empty()) continue;
            bool all_stripped = std::all_of(
                it->second.begin(), it->second.end(),
                [&](const auto & rdep) { return stripped.count(rdep) > 0; });
            if (all_stripped) { stripped.insert(nevra); changed = true; }
        }
    }

    for (const auto & nevra : stripped)
        lock.packages.erase(nevra);

    for (auto & [_, arch_map] : lock.closures)
        for (auto & [_, closure] : arch_map)
            std::erase_if(closure,
                [&](const auto & n) { return stripped.count(n) > 0; });
}


// ── Resolution ────────────────────────────────────────────────────────────────

static void resolve_arch(
    const std::string & arch,
    const std::vector<std::string> & targets,
    const std::vector<std::string> & repodirs,
    const std::string & config_file,
    const std::vector<std::string> & exclude_patterns,
    const std::vector<std::pair<std::string, std::string>> & extra_vars,
    bool gpg_check,
    LockData & lock)
{
    libdnf5::Base base;

    if (!config_file.empty())
        base.get_config().get_config_file_path_option().set(
            libdnf5::Option::Priority::COMMANDLINE, config_file);

    base.load_config();
    base.get_vars()->set("arch", arch, libdnf5::Vars::Priority::RUNTIME);
    for (const auto & [k, v] : extra_vars)
        base.get_vars()->set(k, v, libdnf5::Vars::Priority::RUNTIME);
    base.setup();

    auto sack = base.get_repo_sack();  // RepoSackWeakPtr
    if (!repodirs.empty()) {
        for (const auto & rd : repodirs)
            sack->create_repos_from_dir(rd);
    } else {
        sack->create_repos_from_system_configuration();
    }
    // Load only AVAILABLE repos — do not read the host's RPMDB.
    sack->load_repos(libdnf5::repo::Repo::Type::AVAILABLE);

    // Pre-solver exclusions: excluded packages are invisible to the solver,
    // so their unique transitive deps are also automatically omitted.
    // Resolution fails for any target that hard-requires an excluded package.
    if (!exclude_patterns.empty()) {
        libdnf5::rpm::PackageQuery q(base);
        q.filter_available();
        q.filter_name(exclude_patterns, libdnf5::sack::QueryCmp::GLOB);
        base.get_rpm_package_sack()->add_user_excludes(q);
    }

    for (const auto & target : targets) {
        libdnf5::Goal goal(base);  // fresh Goal per target for independent closures
        goal.add_install(target);
        auto tx = goal.resolve();

        if (tx.get_problems() != libdnf5::GoalProblem::NO_PROBLEM) {
            std::string err;
            for (const auto & log : tx.get_resolve_logs_as_strings()) {
                if (!err.empty()) err += "; ";
                err += log;
            }
            lock.errors[target][arch] = err.empty() ? "resolution failed" : err;
            continue;
        }

        if (gpg_check && !tx.check_gpg_signatures()) {
            std::string err;
            for (const auto & p : tx.get_gpg_signature_problems()) {
                if (!err.empty()) err += "; ";
                err += p;
            }
            lock.errors[target][arch] = "GPG check failed: " + err;
            continue;
        }

        std::vector<std::string> closure;
        for (const auto & tspkg : tx.get_transaction_packages()) {
            auto pkg          = tspkg.get_package();
            const std::string nevra = pkg.get_nevra();
            closure.push_back(nevra);

            if (lock.packages.count(nevra)) continue;  // already seen from another target

            PkgInfo info;
            info.name     = pkg.get_name();
            info.repo_id  = pkg.get_repo_id();
            info.location = pkg.get_location();
            info.sha256   = pkg.get_checksum().get_checksum();
            info.size     = pkg.get_download_size();

            auto provides = pkg.get_provides();
            for (const auto & r : provides)
                if (r.get_name()) info.provides_names.emplace_back(r.get_name());

            auto requires_ = pkg.get_requires();
            for (const auto & r : requires_)
                if (r.get_name()) info.requires_names.emplace_back(r.get_name());

            if (!lock.repos.count(info.repo_id)) {
                auto repo     = pkg.get_repo();
                auto baseurls = repo->get_config().get_baseurl_option().get_value();
                lock.repos[info.repo_id] = baseurls.empty() ? "" : baseurls[0];
            }

            lock.packages.emplace(nevra, std::move(info));
        }

        lock.closures[target][arch] = std::move(closure);
    }
}


// ── Topological sort ──────────────────────────────────────────────────────────
// Sort a package closure in dependency installation order using Kahn's BFS
// algorithm. Packages with no in-closure dependencies come first. Alphabetical
// order is used to break ties for determinism. RPM dependency graphs can have
// cycles; when the queue empties with packages remaining, the cycle is broken
// by force-enqueueing the unprocessed package with the lowest in-degree
// (alphabetically first on ties).
static std::vector<std::string> topo_sort(
    const std::vector<std::string> & closure,
    const std::map<std::string, PkgInfo> & packages)
{
    std::unordered_set<std::string> in_closure(closure.begin(), closure.end());

    // in-degree within the closure; std::map keeps keys sorted for determinism
    std::map<std::string, int> indegree;
    std::map<std::string, std::vector<std::string>> rdeps;
    for (const auto & nevra : closure)
        indegree[nevra] = 0;
    for (const auto & nevra : closure) {
        auto it = packages.find(nevra);
        if (it == packages.end()) continue;
        for (const auto & dep : it->second.deps) {
            if (!in_closure.count(dep)) continue;
            indegree[nevra]++;
            rdeps[dep].push_back(nevra);
        }
    }

    // Seed with zero-in-degree packages (already sorted via std::map iteration)
    std::vector<std::string> queue;
    for (const auto & [nevra, deg] : indegree)
        if (deg == 0)
            queue.push_back(nevra);

    std::vector<std::string> result;
    result.reserve(closure.size());
    int head = 0;

    auto drain = [&]() {
        while (head < (int)queue.size()) {
            const std::string nv = queue[head++];
            indegree[nv] = -1;  // mark processed
            result.push_back(nv);
            std::vector<std::string> newly_zero;
            for (const auto & rdep : rdeps[nv]) {
                if (indegree[rdep] <= 0) continue;
                if (--indegree[rdep] == 0)
                    newly_zero.push_back(rdep);
            }
            std::sort(newly_zero.begin(), newly_zero.end());
            for (const auto & nz : newly_zero)
                queue.push_back(nz);
        }
    };

    drain();

    // Break any cycles by force-picking the minimum-in-degree package
    // (alphabetically first on ties) until all packages are placed.
    while (result.size() != closure.size()) {
        std::string best;
        int best_deg = INT_MAX;
        for (const auto & [nevra, deg] : indegree) {
            if (deg <= 0) continue;
            if (deg < best_deg || (deg == best_deg && nevra < best)) {
                best = nevra;
                best_deg = deg;
            }
        }
        indegree[best] = 0;
        queue.push_back(best);
        drain();
    }

    return result;
}

// ── JSON serialisation ─────────────────────────────────────────────────────────
// Keys are inserted in alphabetical order into every json_object so that
// json-c's insertion-order serialisation produces deterministic output.

static json_object * build_json(const LockData & lock)
{
    json_object * root = json_object_new_object();

    // arches: sorted vector
    json_object * arches_arr = json_object_new_array();
    for (const auto & a : lock.arches)
        json_object_array_add(arches_arr, json_object_new_string(a.c_str()));
    json_object_object_add(root, "arches", arches_arr);

    // packages: std::map iterates in NEVRA-sorted order
    json_object * pkgs_obj = json_object_new_object();
    for (const auto & [nevra, info] : lock.packages) {
        json_object * e = json_object_new_object();
        // Keys alphabetical: deps, path, repo, sha256, size
        json_object * deps_arr = json_object_new_array();
        for (const auto & dep : info.deps)  // std::set iterates sorted
            json_object_array_add(deps_arr, json_object_new_string(dep.c_str()));
        json_object_object_add(e, "deps",   deps_arr);
        json_object_object_add(e, "path",   json_object_new_string(info.location.c_str()));
        json_object_object_add(e, "repo",   json_object_new_string(info.repo_id.c_str()));
        json_object_object_add(e, "sha256", json_object_new_string(info.sha256.c_str()));
        json_object_object_add(e, "size",   json_object_new_int64(info.size));
        json_object_object_add(pkgs_obj, nevra.c_str(), e);
    }
    json_object_object_add(root, "packages", pkgs_obj);

    // repos: std::map iterates in repo_id-sorted order
    json_object * repos_obj = json_object_new_object();
    for (const auto & [repo_id, baseurl] : lock.repos) {
        json_object * r = json_object_new_object();
        json_object_object_add(r, "baseurl", json_object_new_string(baseurl.c_str()));
        json_object_object_add(repos_obj, repo_id.c_str(), r);
    }
    json_object_object_add(root, "repos", repos_obj);

    // targets: collect all target names, output sorted
    json_object * targets_obj = json_object_new_object();
    std::set<std::string> all_targets;
    for (const auto & [t, _] : lock.closures) all_targets.insert(t);
    for (const auto & [t, _] : lock.errors)   all_targets.insert(t);

    for (const auto & target : all_targets) {
        json_object * tobj = json_object_new_object();

        // Collect all arches for this target (sorted via std::set)
        std::set<std::string> target_arches;
        if (lock.closures.count(target))
            for (const auto & [a, _] : lock.closures.at(target)) target_arches.insert(a);
        if (lock.errors.count(target))
            for (const auto & [a, _] : lock.errors.at(target)) target_arches.insert(a);

        for (const auto & arch : target_arches) {
            const bool has_error = lock.errors.count(target) &&
                                   lock.errors.at(target).count(arch);
            if (has_error) {
                json_object * err_obj = json_object_new_object();
                json_object_object_add(err_obj, "error",
                    json_object_new_string(lock.errors.at(target).at(arch).c_str()));
                json_object_object_add(tobj, arch.c_str(), err_obj);
            } else {
                const auto & closure = lock.closures.at(target).at(arch);
                std::vector<std::string> sorted_closure =
                    topo_sort(closure, lock.packages);
                json_object * carr = json_object_new_array();
                for (const auto & nevra : sorted_closure)
                    json_object_array_add(carr, json_object_new_string(nevra.c_str()));
                json_object_object_add(tobj, arch.c_str(), carr);
            }
        }
        json_object_object_add(targets_obj, target.c_str(), tobj);
    }
    json_object_object_add(root, "targets", targets_obj);

    json_object_object_add(root, "version", json_object_new_string("1"));

    return root;
}


// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char * argv[])
{
    std::vector<std::string> arches;
    std::vector<std::string> repodirs;
    std::string              config_file;
    std::vector<std::string> allowed_repos;
    std::vector<std::string> exclude_patterns;
    std::vector<std::string> strip_patterns;
    std::vector<std::pair<std::string, std::string>> extra_vars;

    poptContext opt_ctx =
        poptGetContext(nullptr, argc,
                       const_cast<const char **>(argv),
                       options_table, 0);
    poptSetOtherOptionHelp(opt_ctx,
        "--arch ARCH [--arch ARCH ...] [OPTIONS] PKG [PKG...]");

    int rc;
    while ((rc = poptGetNextOpt(opt_ctx)) > 0) {
        const char * val = poptGetOptArg(opt_ctx);
        switch (rc) {
        case OPT_ARCH:      if (val) arches.emplace_back(val);           break;
        case OPT_REPODIR:   if (val) repodirs.emplace_back(val);         break;
        case OPT_CONFIG:    if (val) config_file = val;                  break;
        case OPT_REPO:      if (val) allowed_repos.emplace_back(val);    break;
        case OPT_EXCLUDE:   if (val) exclude_patterns.emplace_back(val); break;
        case OPT_STRIP:     if (val) strip_patterns.emplace_back(val);   break;
        case OPT_GPGKEYDIR: /* gpgkeydir is informational for now */     break;
        case OPT_VAR:
            if (val) {
                std::string kv(val);
                auto eq = kv.find('=');
                if (eq == std::string::npos) {
                    fprintf(stderr, "dnf5lock: --var requires NAME=VALUE, got: %s\n", val);
                    free(const_cast<char *>(val));
                    poptFreeContext(opt_ctx);
                    return EXIT_FAILURE;
                }
                extra_vars.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            }
            break;
        }
        free(const_cast<char *>(val));
    }

    if (rc < -1) {
        fprintf(stderr, "dnf5lock: %s: %s\n",
                poptBadOption(opt_ctx, POPT_BADOPTION_NOALIAS),
                poptStrerror(rc));
        poptFreeContext(opt_ctx);
        return EXIT_FAILURE;
    }

    if (arches.empty()) {
        fprintf(stderr, "Error: at least one --arch is required\n");
        poptPrintUsage(opt_ctx, stderr, 0);
        poptFreeContext(opt_ctx);
        return EXIT_FAILURE;
    }

    if (!allowed_repos.empty())
        fprintf(stderr,
            "Warning: --repo filtering is not yet implemented; "
            "all configured repos will be used\n");

    std::vector<std::string> targets;
    const char * arg;
    while ((arg = poptGetArg(opt_ctx)) != nullptr)
        targets.emplace_back(arg);
    poptFreeContext(opt_ctx);

    if (targets.empty()) {
        fprintf(stderr, "Error: at least one package target is required\n");
        return EXIT_FAILURE;
    }

    // ── Resolution ────────────────────────────────────────────────────────────

    LockData lock;
    lock.arches = arches;  // preserve user-specified order in arches array

    for (const auto & arch : arches)
        resolve_arch(arch, targets, repodirs, config_file,
                     exclude_patterns, extra_vars, opt_gpgcheck != 0, lock);

    compute_deps(lock);

    if (!strip_patterns.empty())
        apply_strip(lock, strip_patterns);

    // ── Emit lock file ────────────────────────────────────────────────────────

    json_object * root = build_json(lock);
    const char * json_str =
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);

    int exit_code = EXIT_SUCCESS;
    if (opt_output) {
        FILE * f = fopen(opt_output, "w");
        if (!f) {
            perror(opt_output);
            exit_code = EXIT_FAILURE;
        } else {
            fputs(json_str, f);
            fputc('\n', f);
            fclose(f);
        }
    } else {
        fputs(json_str, stdout);
        fputc('\n', stdout);
    }

    json_object_put(root);
    return exit_code;
}
