import struct
import sys
import socket
import re
import select
import traceback
from time import sleep

petr_header = "IBxxx"

port = int(sys.argv[1])
servaddr = ('localhost', port)

clients = {}

rooms = {}

server_protocol = {
    0x0: 'OK',
    0x20: 'ANCREATE',
    0x22: 'ANCLOSED',
    0x23: 'ANLIST',
    0x24: 'ANWATCH',
    0x27: 'ANUPDATE',
    0x32: 'USRLIST',
    0x33: 'USRWINS',
    0x34: 'USRSALES',
    0x35: 'USRBLNC',
    0x1A: 'EUSRLGDIN',
    0x1B: 'EWRNGPWD',
    0x2B: 'EANFULL',
    0x2C: 'EANNOTFOUND',
    0x2D: 'EANDENIED',
    0x2E: 'EBIDLOW',
    0x2F: 'EINVALIDARG',
    0xFF: 'ESERV',

}

client_protocol = {
    "LOGIN": 0x10,
    "LOGOUT": 0x11,
    "ANCREATE": 0x20,
    "ANLIST": 0x23,
    "ANWATCH": 0x24,
    "ANLEAVE": 0x25,
    "ANBID": 0x26,
    "ANUPDATE": 0x27,
    "USRLIST": 0x32,
    "USRWINS": 0x33,
    "USRSALES": 0x34,
    "USRBLNC": 0x35
}


def unescape(input: str) -> str:
    '''
    converts literal \r and \n to '\r' and '\n' characters
    :param input: input string
    :return: output string
    '''
    return input.replace("\\r", "\r").replace("\\n", "\n")


def header_bytes(msg_type, msg_len):
    return struct.pack(petr_header, msg_len, msg_type)


def header_data(buf):
    msg_len, msg_type = struct.unpack(petr_header, buf)
    return msg_type, msg_len


def send_msg(conn, msg_type, msg_body):
    if not msg_body:
        conn.send(header_bytes(msg_type, 0))
        return
    header = header_bytes(msg_type, len(msg_body) + 1)
    body = msg_body.encode('ascii')
    body += '\x00'.encode('ascii')
    header += body
    conn.send(header)


def print_recv(resp_type, body=None, file=sys.stdout):
    if not body:
        print(f'RECV <- {server_protocol[resp_type]}', file=file)
    elif server_protocol[resp_type] == 'USRLIST':
        rm_listrooms = body.split('\n')
        if len(rm_listrooms) > 1:  # should be true
            # make sure missing null terminator is visible in the output
            if rm_listrooms[-1] == '\0':
                rm_listrooms = rm_listrooms[:-1]
                rm_listrooms.sort()
                body = '\n'.join((*rm_listrooms, '\0'))
            else:
                rm_listrooms.sort()
                body = '\n'.join(rm_listrooms)
        print(f'RECV <- {server_protocol[resp_type]} {repr(body)}', file=file)
    else:
        print(f'RECV <- {server_protocol[resp_type]} {repr(body)}', file=file)


poller = select.poll()
def read_all():
    to_read = poller.poll(0)
    for fd, mask in to_read:
        client = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
        header = client.recv(8, socket.MSG_WAITALL)
        resp_type, resp_len = header_data(header)
        body = None
        if resp_len != 0:
            body = client.recv(resp_len, socket.MSG_WAITALL).decode('ascii')

        print_recv(resp_type, body)


command_re = re.compile(r'(?P<cmd>[A-Z]+)(\s(?P<arg>.+))?\n')
try:
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(servaddr)
    poller.register(client, select.POLLIN)

    for line in sys.stdin:
        if len(line.strip()) == 0:
            continue

        command = command_re.match(line)
        if not command:
            print("Invalid command!")
            continue
        command = command.groupdict()

        cmd = command['cmd']
        args = unescape(command['arg']) if command['arg'] else None

        # printing out the data that will be sent in readable format
        if args is None:
            print(f"SEND -> {cmd}")
        else:
            # + "\0" is hacky but consistently represents what is sent over the socket
            pargs = args + "\0"
            # repr args to make whitespace readable again
            print(f"SEND -> {cmd} {repr(pargs)}")

        send_msg(client, client_protocol[cmd], args)

        sleep(0.1)  # wait for server to respond
        read_all()

        if cmd == 'LOGOUT':
            break

except Exception as e:
    print("Oh no, the client had an error!:", e, file=sys.stderr)
    traceback.print_exc(file=sys.stderr)
