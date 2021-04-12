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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
inet_addr_default="10.10.10.30"

usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script enables and disables interface through Wifi_Inet_Config table
        Script fails if the interface is not created in Wifi_Inet_State table or it is not present on the system with given
        configuration
Arguments:
    -h  show this help message
    \$1 (if_name)   : used as if_name in Wifi_Inet_Config table   : (string)(required)
    \$2 (if_type)   : used as if_type in Wifi_Inet_Config table   : (string)(required)
    \$3 (inet_addr) : used as inet_addr in Wifi_Inet_Config table : (string)(optional) : (default:${inet_addr_default})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <IF-TYPE>
Script usage example:
   ./${tc_name} eth0.100 vlan
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

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1
if_type=$2
inet_addr=${3:-${inet_addr_default}}

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    fut_info_dump_line
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: NM2 test - Remove reinsert interface"

log "$tc_name: Creating $if_type interface"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -if_type "$if_type" \
    -netmask "255.255.255.0" \
    -inet_addr "$inet_addr" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $inet_addr was properly applied to $if_name (ENABLED #1)"
wait_for_function_response 0 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Settings applied to ifconfig (ENABLED #1) - $if_name" ||
    raise "Failed to apply settings to ifconfig (ENABLED #1) - $if_name" -l "$tc_name" -tc

log "$tc_name: Setting ENABLED to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=false" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $inet_addr was properly removed from $if_name (DISABLED #1)"
wait_for_function_response 1 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Settings removed from ifconfig (DISABLE #1) - $if_name" ||
    raise "Failed to remove settings from ifconfig (DISABLE #1) - $if_name" -l "$tc_name" -tc

log "$tc_name: Setting ENABLED to true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=true" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=true" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=true" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=true" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $inet_addr was properly applied to $if_name (ENABLED #1)"
wait_for_function_response 0 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Settings applied to ifconfig (ENABLED #2) - $if_name" ||
    raise "Failed to apply settings to ifconfig (ENABLED #2) - $if_name" -l "$tc_name" -tc

log "$tc_name: Setting ENABLED to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=false" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $inet_addr was properly removed from $if_name (DISABLED #1)"
wait_for_function_response 1 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Settings removed from ifconfig (DISABLE #2) - $if_name" ||
    raise "Failed to remove settings from ifconfig (DISABLE #2) - $if_name" -l "$tc_name" -tc

log "$tc_name: Setting ENABLED to true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=true" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=true" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=true" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=true" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS was properly applied to $if_name (ENABLED #1)"
wait_for_function_response 0 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Settings applied to ifconfig (ENABLED #3) - enabled=true" ||
    raise "Failed to apply settings to ifconfig (ENABLED #3) - enabled=true" -l "$tc_name" -tc

pass
