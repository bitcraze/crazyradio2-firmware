---
title: Cortex debugging
page_id: cortex_debugging
---

For VS code, follow the prerequisites of the [Crazyflie's STM debugging instructions](https://www.bitcraze.io/documentation/repository/crazyflie-firmware/master/development/openocd_gdb_debugging/).

Open vscode in the crazyradio2-firmware folder and use this launch.json file instead:

```
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Cortex Debug",
            "cwd": "${workspaceRoot}",
            "executable": "build/zephyr/crazyradio2.elf",
            "request": "launch",
            "type": "cortex-debug",
            "showDevDebugOutput": "false",
            "servertype": "jlink",
            "device": "nrf52",
            "runToMain": true,
        },
    ]

}
```
