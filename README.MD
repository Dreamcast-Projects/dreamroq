
<!-- PROJECT LOGO -->
<br />
<div align="center">

  <h1 align="center">Dreamroq Library</h3>

</div>

<!-- ABOUT THE PROJECT -->
## About The Project
Dreamroq is a RoQ playback library specifically designed for the Sega Dreamcast video game console. RoQ is a relatively simple video file format developed for video-heavy CD-ROM games. Learn more about the RoQ format [HERE](http://wiki.multimedia.cx/index.php?title=RoQ).

The Dreamroq library consists of two parts: the decoder (dreamroqlib.h/c) and the player (roq-player.h/c). The decoder was initially written by Mike Melanson and later updated by Josh "PH3NOM" Pearson to include audio decoding. The dreamcast-player.c was replaced with two files, roq-player.h/c, to simplify the integration of the library into Dreamcast applications. The player component is designed to run under the KallistiOS (KOS) open-source operating system. Learn more about [KOS](https://github.com/KallistiOS/KallistiOS).

The library also features a sample testing utility that can be built and executed on Unix systems, which is useful for debugging and validation.

Find more RoQ sample files [HERE](http://samples.mplayerhq.hu/game-formats/idroq/).

RoQ files can be created using the [FFMEG](http://ffmpeg.org/) encoder:

```ffmpeg -i input.mp4 -r 30 -ar 22050 -s 512x512 output.roq -threads 2```

This command will convert your input video into a 512x512, 30 fps RoQ video with 22050 Hz audio.


<!-- Building (Dreamcast) -->
## Building for Dreamcast

To build for the Sega Dreamcast, simply run:

```make```

This command will generate a file named dreamroq-player.elf, which can be loaded onto a Dreamcast console using standard methods (outside the scope of this document).

The main.c file contains a hardcoded RoQ romdisk file path (/rd/) in its main function. Ideally, this should point to a file burned onto an optical disc.

<!-- Building (Unix) -->
## Building for Unix

Dreamroq includes two Makefiles. The first, invoked by running a bare make command as described in the "Building for Dreamcast" section, builds the Dreamcast application. The second Makefile, Makefile.PC, can be invoked with:

```make -f Makefile.PC```

in the source directory. This command will build the test-dreamroq executable utility with the following usage:

```./test-dreamroq <file.roq>```

This utility decodes the RoQ file from the command line into a series of PNM files and a .wav file in the extract directory (note: this process could consume a significant amount of disk space).

<!-- LICENSE -->
## License

Dreamroq is designed to be license-compatible with the rest of the KallistiOS operating system, which uses a BSD-style open-source license. You can read the specific license text in LICENSE.KOS.


<!-- ACKNOWLEDGMENTS -->
## Credits

* Library originally written by Mike Melanson (mike -at- multimedia.cx)
* Audio support added by Josh "PH3NOM" Pearson (ph3nom.dcmc@gmail.com)
* Inspired by [Ian Michael](https://github.com/ianmicheal)

