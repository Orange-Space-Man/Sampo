Early stages, currently does the bare minimum with default mod support + Sampo mod support (mods using sampo lua functions)
Brought over the existing functions from Beefcake as well as added several more, no issues found yet but to be fair I haven't gone through and tested every single lua function yet to confirm there are no bugs with the code.

Future plans:
Example mods to test Sampo lua functions
Configurable settings
QoL improvements that do not change gameplay completely, these will be possible to enable/disable
Full script explorer & debugger
Component explorer

Will build a proxy winmm.dll that needs to be placed inside of C:\Program Files (x86)\Steam\steamapps\common\Noita

This is being built off the idea of this abandoned project [Beefcake](https://github.com/BoldlyGo88/Beefcake)

In its current state this is completely usable as an alternative mod loader/manager. 
It DOES block Noita's mod check, you will get achievements, unlocks, etc while using mods with this. 