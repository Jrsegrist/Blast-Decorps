"""--model stand-ins for osRecvMesg / osSendMesg that move messages through the
OSMesgQueue in guest memory (for thread loops such as the front end's save
thread fe_pak_thread, which never return):

    --model osRecvMesg=mesgqueue.py:recv --model osSendMesg=mesgqueue.py:send

OSMesgQueue: mtqueue 0x0, fullqueue 0x4, validCount 0x8, first 0xC,
msgCount 0x10, msg 0x14.

recv: a queue with messages pops the first one (into *msg when msg != NULL)
and returns 0.  An empty queue: OS_MESG_NOBLOCK returns -1; OS_MESG_BLOCK on
the queue named by STOP_QUEUE ends the run (the thread would wait for its
next command), on any other queue it delivers message 0 (an event that
"arrived").
send: appends when there is room and returns 0; a full queue returns -1
(OS_MESG_NOBLOCK) or ends the run (OS_MESG_BLOCK, the sender would wait).
"""

STOP_QUEUE = "gPakRequestQueue"       # the save thread's command queue


def _q(mem, mq):
    return mem.s32(mq + 8), mem.s32(mq + 0xC), mem.s32(mq + 0x10), mem.u32(mq + 0x14)


def recv(args, mem):
    mq, msgp, flag = args[0] & 0xFFFFFFFF, args[1] & 0xFFFFFFFF, args[2]
    valid, first, count, buf = _q(mem, mq)
    if valid <= 0 or count <= 0:
        if flag == 0:
            return 0xFFFFFFFF
        if mq == mem.sym(STOP_QUEUE):
            mem.stop()
        if msgp:
            mem.w32(msgp, 0)
        return 0
    if msgp:
        mem.w32(msgp, mem.u32(buf + 4 * first))
    mem.w32(mq + 0xC, (first + 1) % count)
    mem.w32(mq + 8, valid - 1)
    return 0


def send(args, mem):
    mq, msg, flag = args[0] & 0xFFFFFFFF, args[1], args[2]
    valid, first, count, buf = _q(mem, mq)
    if count <= 0 or valid >= count:
        if flag == 0:
            return 0xFFFFFFFF
        mem.stop()
    mem.w32(buf + 4 * ((first + valid) % count), msg)
    mem.w32(mq + 8, valid + 1)
    return 0
