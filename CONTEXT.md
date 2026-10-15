# Zandronum ARM Client

This project concerns a playable ARM port of Zandronum 3.2.1 and its use on the owner's R36S handheld.

## Language

**ARM client**:
A player-facing Zandronum program that runs on ARM hardware, presents the game, and accepts player input. A dedicated server is a different deliverable.
_Avoid_: ARM support, when it leaves client support unspecified

**Release baseline**:
The official Zandronum release whose behavior and multiplayer compatibility the ARM client preserves. The selected release is 3.2.1.
_Avoid_: latest, when the intended release is 3.2.1

**Legacy ARM fork**:
The existing Pandora-derived Zandronum code in this repository, used as evidence of earlier ARM porting work.
_Avoid_: the ARM64 release, when referring to this unverified legacy fork

**R36S client**:
The ARM client adapted for play on the owner's R36S handheld running dArkOS and launched through PortMaster.
_Avoid_: R36S support, when it leaves actual handheld gameplay unspecified

**Stock-server compatibility**:
The ability to join and play on an unmodified official Zandronum 3.2.1 server alongside existing PC clients using matching game content.
_Avoid_: multiplayer works, without identifying the server version and content

**Handheld content setup**:
Controller-operated preparation of a selected server's required add-ons, including obtaining available missing files and matching their versions and load order before joining. Base-game IWADs are supplied separately by the owner.
_Avoid_: content setup, when it only means copying files from a desktop
