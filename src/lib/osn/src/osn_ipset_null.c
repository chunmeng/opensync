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

#include <ctype.h>
#include <unistd.h>

#include "execsh.h"
#include "log.h"
#include "util.h"

#include "osn_ipset.h"

/** Maximum length of an ipset */
#define OSN_IPSET_NAME_LEN      32

struct osn_ipset
{
    /** ipset name */
    char                ips_name[OSN_IPSET_NAME_LEN];
};

osn_ipset_t* osn_ipset_new(
        const char *name,
        enum osn_ipset_type type,
        const char *options)
{
    (void)type;
    (void)options;

    osn_ipset_t *self;

    self = calloc(1, sizeof(*self));
    STRSCPY(self->ips_name, name);

    return self;
}

/**
 * IPSET deinitialization function.
 */
void osn_ipset_del(osn_ipset_t *self)
{
    free(self);
}

/**
 * Apply
 */
bool osn_ipset_apply(osn_ipset_t *self)
{
    (void)self;
    return true;
}

/**
 * Use `ipset swap` to guarantee some atomicity when replacing the values in the set
 */
bool osn_ipset_values_set(osn_ipset_t *self, const char *values[], int values_len)
{
    (void)self;
    (void)values;
    (void)values_len;

    return true;
}


/**
 * Add values from set
 */
bool osn_ipset_values_add(osn_ipset_t *self, const char *values[], int values_len)
{
    (void)self;
    (void)values;
    (void)values_len;

    return true;
}


/**
 * Removes values from set
 */
bool osn_ipset_values_del(osn_ipset_t *self, const char *values[], int values_len)
{
    (void)self;
    (void)values;
    (void)values_len;

    return true;
}
