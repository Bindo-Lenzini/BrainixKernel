// BrainixKernel — user/syscall.S
.section .text

.global write
.global read
.global exit
.global open
.global close
.global fd_restrict
.global yield
.global brk
.global exec
.global fork
.global wait
.global readdir
.global pipe
.global ipc_pair
.global ipc_pair_rights
.global ipc_send
.global ipc_recv
.global ipc_send_wait
.global ipc_recv_wait
.global ipc_send_endpoint
.global ipc_recv_endpoint
.global ipc_map
.global dup2
.global socket
.global bind
.global sendto
.global recvfrom
.global connect
.global send
.global recv
.global chmod
.global chown
.global getgroups
.global setgroups

write:
    mov x8, #0
    svc #0
    ret

read:
    mov x8, #3
    svc #0
    ret

exit:
    mov x8, #1
    svc #0
    ret

open:
    mov x8, #4
    svc #0
    ret

close:
    mov x8, #5
    svc #0
    ret

fd_restrict:
    mov x8, #37
    svc #0
    ret

yield:
    mov x8, #2
    svc #0
    ret

brk:
    mov x8, #6
    svc #0
    ret

exec:
    mov x8, #7
    svc #0
    ret

fork:
    mov x8, #8
    svc #0
    ret

wait:
    mov x8, #9
    svc #0
    ret

readdir:
    mov x8, #10
    svc #0
    ret

pipe:
    mov x8, #11
    svc #0
    ret

ipc_pair:
    mov x8, #20
    svc #0
    ret

ipc_pair_rights:
    mov x8, #36
    svc #0
    ret

ipc_send:
    mov x8, #21
    svc #0
    ret

ipc_recv:
    mov x8, #22
    svc #0
    ret

ipc_send_wait:
    mov x8, #32
    svc #0
    ret

ipc_recv_wait:
    mov x8, #33
    svc #0
    ret

ipc_send_endpoint:
    mov x8, #34
    svc #0
    ret

ipc_recv_endpoint:
    mov x8, #35
    svc #0
    ret

ipc_map:
    mov x8, #31
    svc #0
    ret

dup2:
    mov x8, #12
    svc #0
    ret

socket:
    mov x8, #13
    svc #0
    ret

sendto:
    mov x8, #14
    svc #0
    ret

recvfrom:
    mov x8, #15
    svc #0
    ret

bind:
    mov x8, #16
    svc #0
    ret

connect:
    mov x8, #17
    svc #0
    ret

send:
    mov x8, #18
    svc #0
    ret

recv:
    mov x8, #19
    svc #0
    ret

chmod:
    mov x8, #29
    svc #0
    ret

chown:
    mov x8, #30
    svc #0
    ret

getgroups:
    mov x8, #27
    svc #0
    ret

setgroups:
    mov x8, #28
    svc #0
    ret
