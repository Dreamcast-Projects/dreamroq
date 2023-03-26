
<!-- PROJECT LOGO -->
<br />
<div align="center">

  <h3 align="center">Dreamroq Library</h3>

</div>

<!-- ABOUT THE PROJECT -->
## About The Project

Dreamroq is a RoQ playback library designed for the Sega Dreamcast video
game console.

RoQ is a relatively simple video file format developed for video-heavy
CD-ROM games. Read more about the format here:

http://wiki.multimedia.cx/index.php?title=RoQ

The Dreamroq library includes a player component that is designed to run
under the KallistiOS (KOS) open source operating system. Read more about
KOS at:

http://gamedev.allusion.net/softprj/kos/

The library also includes a sample testing utility that can be built
and executed on Unix systems. This utility is useful for debugging and
validation.

RoQ sample files can be found at:

http://samples.mplayerhq.hu/game-formats/idroq/

RoQ files can also be created using the Switchblade encoder:

http://icculus.org/~riot/

A version of Switchblade is also included in FFmpeg and many derivative
programs:

http://ffmpeg.org/

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- Building (KOS) -->
### Building (KOS)

To build for the Sega Dreamcast, simply type:

```make```

This is a standard KOS Makefile which assumes that a KOS build environment is 
available. This is covered in the KOS documentation. This step will build a file 
named dreamroq-player.elf which can be loaded onto a Dreamcast console via 
standard means (also outside the scope of this document).

The file main.c contains a hardcoded RoQ file path in its main function. It is 
best if this points to a file burned on an optical disc. It is also viable to 
build a small RoQ file as a ROM disk into the ELF file (which is well supported 
in KOS) and load the file from the '/rd' mount point.


<!-- Building (Unix) -->
### Building (Unix)

There are 2 Makefiles included with Dreamroq. The first -- implicitly invoked when running 
a bare 'make' command as seen in the "Building (KOS)" section -- builds the dreamcast application. 
The second Makefile is Makefile.PC, invoked with:

  ```make -f Makefile.PC```

in the source directory. This will build the executable test-dreamroq. This
utility has the following usage:

  ```./test-dreamroq <file.roq>```

This will decode the RoQ file from the command line into a series of PNM
files and a .wav in the extract directory (watch out-- this could take up a
lot of disk space).


<!-- LICENSE -->
## License

Dreamroq is meant to be license-compatible with the rest of the KallistiOS
operating system, which is a BSD-style open source license. You can read
the specific text in LICENSE.KOS.

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

Library originally written by Mike Melanson (mike -at- multimedia.cx)

Audio support added by Josh "PH3NOM" Pearson (ph3nom.dcmc@gmail.com)

[Ian Michael](https://github.com/ianmicheal)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

