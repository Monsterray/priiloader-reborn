---------------------------------------------------------------
* Priiloader Installer - An installation utiltiy for priiloader *
---------------------------------------------------------------
==========================
Binary Distribution Notes
==========================

This installer/removal tool will install Priiloader
any previous version and can be installed over the top of an older install. There is also
a removal option that will restore your system menu and clean all traces of Priiloader
from your system.

BEFORE YOU START: make a BootMii NAND backup. The installer writes the NAND.

Usage:

Press (+/A) to install or update Priiloader. An update keeps your settings, hacks and password.
Press (-/Y) to remove Priiloader and restore your system menu.
Press (HOME/Start) to chicken out and quit the installer!

To access Priiloader and configure it please hold (RESET) during the boot cycle.
Priiloader can also update itself: use "Check For Update" in its menu.

Without buttons: run the installer with the argument install, update or remove
(for example from wiiload). The result goes to sd:/priiloader/installer_result.txt.

If something goes wrong at boot, switch on "Dump Gecko output" in Priiloader's
settings and look at sd:/prii.log.

Requirements:

if run from HBC 1.0.7 and above, no ios patching is needed at all. however, if the installer
has failed to get root access of the nand, it will try to patch IOS. if that fails, you can not install priiloader at this time.

Priiloader itself does not need any ios patching (and we recommend to keep the system menu ios unpatched)


Disclaimer:

This tool comes without any warranties and we accept no liability if you turn your wii into a
paper weight with it. I have tested this tool with 100% success on 23+ WII's. Hopefully you
will have no serious issues with it!

Scam Warning:
that this is a -FREE- tool. if you paid to get this software in any way (during a guide or got a link/media
containing the software after paying) , you have been scammed and you should demand a refund and/or sue the bastard
as this is released under GPL they are most likely not following that rule either (stating they should add the
license to the package and if code was changed add a note what code was changed , like a diff/patch file)

Kudos:

Crediar (base source preloader 0.30)
DacoTaco and BadUncle (Priiloader)
Phpgeek (additional mods & installer)
#wiidev (both of them)
The priiloader-reborn contributors (https://github.com/Monsterray/priiloader-reborn)


DacoTaco and the priiloader-reborn contributors