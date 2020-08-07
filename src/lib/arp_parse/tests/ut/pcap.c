/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Frame (42 bytes) */
static const unsigned char pkt134[42] = {
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x05, /* ........ */
0x1b, 0xd1, 0xa5, 0x7b, 0x08, 0x06, 0x00, 0x01, /* ...{.... */
0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x00, 0x05, /* ........ */
0x1b, 0xd1, 0xa5, 0x7b, 0x0a, 0x01, 0x00, 0x2b, /* ...{...+ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x01, /* ........ */
0x00, 0x0c                                      /* .. */
};

/* Frame (60 bytes) */
static const unsigned char pkt135[60] = {
0x00, 0x05, 0x1b, 0xd1, 0xa5, 0x7b, 0x60, 0xb4, /* .....{`. */
0xf7, 0xf0, 0x35, 0x7e, 0x08, 0x06, 0x00, 0x01, /* ..5~.... */
0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x60, 0xb4, /* ......`. */
0xf7, 0xf0, 0x35, 0x7e, 0x0a, 0x01, 0x00, 0x0c, /* ..5~.... */
0x00, 0x05, 0x1b, 0xd1, 0xa5, 0x7b, 0x0a, 0x01, /* .....{.. */
0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* .+...... */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00                          /* .... */
};

/* Generated with sudo arping -U -P 192.168.40.111 */
/* Frame (60 bytes) */
static const unsigned char pkt42[60] = {
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xe0, /* ........ */
0x4c, 0x20, 0x9f, 0x85, 0x08, 0x06, 0x00, 0x01, /* L ...... */
0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x00, 0xe0, /* ........ */
0x4c, 0x20, 0x9f, 0x85, 0xc0, 0xa8, 0x28, 0x6f, /* L ....(o */
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xa8, /* ........ */
0x28, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* (o...... */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00                          /* .... */
};
