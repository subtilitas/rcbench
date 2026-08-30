# Why rcbench does not do BLHeli_32 parameters

BLHeli_32 stores its settings as a 256-byte block at a fixed flash address, and
that block is XTEA ciphertext — 64-bit block, 128-bit key, with three plaintext
bytes packed into every four stored bytes.

Everything around it is ordinary protocol and fully implemented from the
specification: the bootloader on the signal wire, the 4-way interface framing,
MSP passthrough, both CRCs, the MCU signature table. We can connect to a
BLHeli_32 ESC, identify it, and read its 256 bytes.

**We cannot say what a single one of them means.**

## The key

The key exists in exactly one place: inside BLHeliSuite32, a closed-source,
unlicensed binary from a project whose author has stopped publishing and whose
update servers are gone. There is no licence to obtain, no vendor to ask, and
no second source.

Lifting it out of the binary and shipping it is not something a technical
decision can make lawful — it is circumventing a protection measure on a
commercial product, and doing so in a repository we ship under MIT with the
standing rule that every protocol here is written from a specification rather
than from somebody else's code.

So the blocker is not effort and never was. Given the BLHeli_S implementation,
reaching a BLHeli_32 bootloader costs one CRC routine and one command table.
What is missing is a key we would have to take rather than be given.

## What we do instead

Identification works without any of it — the bench names the ESC's MCU and
bootloader revision from the four bytes `cmd_DeviceInitFlash` returns.

Direction, direction reversal, 3D mode, beacon and save-settings are DShot
special commands on the signal wire, open and standard, and they cover most of
what a user changes at a bench. Telemetry is unaffected.

A BLHeli_32 ESC on this bench is a recognised, drivable, measurable ESC with a
short parameter list and an honest reason for it.

## What would change this

- A published key or specification from the author.
- A licence from whoever holds the rights now.
- An ESC reflashed to **AM32**, which is open and which this bench will
  programme fully.
