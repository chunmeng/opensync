#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# FUT environment loading
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="cm2/$(basename "$0")"
cm_setup_file="cm2/cm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Test script validate Connection_Manager_Uplink:has_L2 proper behaviour
    -
Arguments:
    -h : show this help message
    \$1 (if_name_l2) : used as L2 interface : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${cm_setup_file} (see ${cm_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME-L2>
Script usage example:
    ./${tc_name} eth0
usage_string
}
while getopts h option; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            echo "Unknown argument" && exit 1
            ;;
    esac
done

check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "${tc_name}" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap '
fut_info_dump_line
print_tables Connection_Manager_Uplink
fut_info_dump_line
interface_bring_up "$if_name" || true
check_restore_management_access || true
run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

if_name=$1

log_title "$tc_name: CM2 test - has_L2 validation"

log "$tc_name: Dropping interface $if_name"
interface_bring_down "$if_name" &&
    log "$tc_name: Interface $if_name is down" ||
    raise "Couldn't bring down interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for has_L2 -> false on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 false &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L2 -> false" ||
    raise "Connection_Manager_Uplink - {has_L2:=false}" -l "$tc_name" -ow

log "$tc_name: Bringing up interface $if_name"
interface_bring_up "$if_name" &&
    log "$tc_name: Interface $if_name is up" ||
    raise "Couldn't bring up interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L2 -> true for ifname==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 true &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L2 -> true" ||
    raise "Connection_Manager_Uplink - {has_L2:=true}" -l "$tc_name" -ow

pass
