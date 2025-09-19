#!/bin/env bash

# Note that ./scmi_telemetry/tlm-0/all_des_enable is write only.
# Even if you're able to read a value from it, that value is undefined and should not be checked during testing

echo 'Enable one at a time, then bulk disable'
echo -n 0 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 1 >./scmi_telemetry/tlm-0/des/0x0000/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 1 >./scmi_telemetry/tlm-0/des/0x0016/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 0 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo 'Disable one at a time, then bulk enable'
echo -n 1 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 0 >./scmi_telemetry/tlm-0/des/0x0000/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 0 >./scmi_telemetry/tlm-0/des/0x0016/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 1 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo 'Mix of enables/disables, then bulk disable'
echo -n 1 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 0 >./scmi_telemetry/tlm-0/des/0x0000/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 0 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo 'Mix of enables/disables, then bulk enable'
echo -n 0 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 1 >./scmi_telemetry/tlm-0/des/0x0000/enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -

echo -n 1 >./scmi_telemetry/tlm-0/all_des_enable
echo | cat ./scmi_telemetry/tlm-0/des/0x0000/enable ./scmi_telemetry/tlm-0/des/0x0016/enable -
