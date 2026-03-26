# util-linux for BCR notes

Used some AI agent to migrate the library into Bazel

## Targets

### libs

@util-linux//libblkid:libblkid
@util-linux//libfdisk:libfdisk
@util-linux//liblastlog2:liblastlog2
@util-linux//libmount:libmount
@util-linux//libsmartcols:filter_grammar
@util-linux//libsmartcols:libsmartcols
@util-linux//libuuid:libuuid

### binaries

@util-linux//disk-utils:addpart
@util-linux//disk-utils:blockdev
@util-linux//disk-utils:delpart
@util-linux//disk-utils:fdformat
@util-linux//disk-utils:fdisk
@util-linux//disk-utils:fsck
@util-linux//disk-utils:fsck.minix
@util-linux//disk-utils:isosize
@util-linux//disk-utils:mkfs
@util-linux//disk-utils:mkfs.bfs
@util-linux//disk-utils:mkfs.minix
@util-linux//disk-utils:mkswap
@util-linux//disk-utils:partx
@util-linux//disk-utils:resizepart
@util-linux//disk-utils:sfdisk
@util-linux//disk-utils:swaplabel
@util-linux//liblastlog2:test_lastlog2_dlopen
@util-linux//liblastlog2:test_lastlog2_pam_lastlog2_output
@util-linux//liblastlog2:test_lastlog2_remove_entry
@util-linux//liblastlog2:test_lastlog2_rename_user
@util-linux//liblastlog2:test_lastlog2_write_read_user
@util-linux//liblastlog2:test_lastlog2_y2038_ll2_read_all
@util-linux//liblastlog2:test_lastlog2_y2038_sqlite3_time
@util-linux//libmount:test_mount_cache
@util-linux//libmount:test_mount_context
@util-linux//libmount:test_mount_debug
@util-linux//libmount:test_mount_lock
@util-linux//libmount:test_mount_monitor
@util-linux//libmount:test_mount_optlist
@util-linux//libmount:test_mount_optstr
@util-linux//libmount:test_mount_tab
@util-linux//libmount:test_mount_tab_diff
@util-linux//libmount:test_mount_tab_update
@util-linux//libmount:test_mount_utils
@util-linux//libmount:test_mount_version
@util-linux//libuuid:test_uuid
@util-linux//libuuid:test_uuid_time
@util-linux//misc-utils:blkid
@util-linux//misc-utils:cal
@util-linux//misc-utils:exch
@util-linux//misc-utils:fadvise
@util-linux//misc-utils:fincore
@util-linux//misc-utils:findfs
@util-linux//misc-utils:findmnt
@util-linux//misc-utils:getopt
@util-linux//misc-utils:hardlink
@util-linux//misc-utils:kill
@util-linux//misc-utils:lastlog2
@util-linux//misc-utils:logger
@util-linux//misc-utils:look
@util-linux//misc-utils:lsblk
@util-linux//misc-utils:lsclocks
@util-linux//misc-utils:lslocks
@util-linux//misc-utils:mcookie
@util-linux//misc-utils:namei
@util-linux//misc-utils:pipesz
@util-linux//misc-utils:rename
@util-linux//misc-utils:uuidd
@util-linux//misc-utils:uuidgen
@util-linux//misc-utils:uuidparse
@util-linux//misc-utils:waitpid
@util-linux//misc-utils:whereis
@util-linux//misc-utils:wipefs
@util-linux//schedutils:chrt
@util-linux//schedutils:coresched
@util-linux//schedutils:ionice
@util-linux//schedutils:taskset
@util-linux//schedutils:uclampset
@util-linux//term-utils:agetty
@util-linux//term-utils:mesg
@util-linux//term-utils:script
@util-linux//term-utils:scriptreplay
@util-linux//term-utils:wall
@util-linux//term-utils:write
@util-linux//text-utils:bits
@util-linux//text-utils:col
@util-linux//text-utils:colcrt
@util-linux//text-utils:colrm
@util-linux//text-utils:column
@util-linux//text-utils:hexdump
@util-linux//text-utils:line
@util-linux//text-utils:rev
