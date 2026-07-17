# AssRender-Vapoursynth

AssRender-Vapoursynth is a Vapoursynth api3 plugin that renders ASS / SSA and SRT (without the HTML-like markup) subtitles, based on the source of pinterf’s fork. It uses libass to render the subtitles, which makes it the fastest, lower memory usage and most correct ASS renderer for Vapoursynth. Now (version >= 0.37.1) it also provides csri interface, you can use it with some support softwares, such as Aegisub.

This also means that it is much more picky about script syntax than VSFilter and friends, so keep that in mind before blaming the filter. Yes, people have reported a lot of errors that were actually the script author’s fault.

## Vapoursynth Usage

Supported video formats:
  - 8-, 10-, 12-, 14- and 16-bit integer
  - YUV, RGB, GRAY

### TextSub

`assrender.TextSub(clip clip, string file, [string vfr, int hinting=0, float scale=1.0, float line_spacing=1.0, float dar, float sar, bool set_default_storage_size=True, int top=0, int bottom=0, int left=0, int right=0, string charset, int debuglevel, string fontdir="", string srt_font="sans-serif", string colorspace])`

Like `sub.TextFile`, `xyvsf.TextSub`

- `clip`: Input video clip.

- `file`: Your subtitle file. May be ASS, SSA or SRT (without the HTML-like markup).
	
- `vfr`: Specify timecodes v1 or v2 file when working with VFRaC.
	
- `hinting`: Font hinting mode. Choose between none (0, default), light (1), normal (2) and Freetype native (3) autohinting.
	
- `scale`: Font scale. Defaults to 1.0.
	
- `line_spacing`: Line spacing in pixels. Defaults to 1.0 and won’t be scaled with frame size.
	
- `frame_width`, `frame_height`: Actual displayed size, provide more information than `dar`&`sar`. Of course you need to set both parameters.
	
- `dar`, `sar`: Aspect ratio, less priority than `frame_width`&`frame_height`. You need to set both parameters, too.

- `set_default_storage_size`: Whether to render ASS according to storage size, default is True. It’s behavior is consistent with the latest libass, but it maybe show some incorrect display effects, you can see *[Fix rendering with libass to match xy-VSFilter](https://github.com/wangqr/Aegisub/pull/134)* for details.
	
- `top`, `bottom`, `left`, `right`: Margins. They will be added to the frame size and may be negative.
	
- `charset`: Character set to use, in GNU iconv or enca format. Defaults to detect the BOM and fallback to UTF-8 if BOM not found. Example enca format: `enca:pl:cp1250` (guess the encoding for Polish, fall back on `cp1250`)
		
- `debuglevel`: How much crap assrender is supposed to spam to stderr.
	
- `fontdir`: Additional font directory. Useful if you are lazy but want to keep your system fonts clean. Default value: `""`

- `srt_font`: Font to use for SRT subtitles. Defaults to whatever Fontconfig chooses for “sans-serif”.
	
- `colorspace`: The color space of your (YUV) video. Possible values:
  - Rec2020, BT.2020
  - Rec709, BT.709
  - Rec601, BT.601
  - PC.709
  - PC.601
  - TV.fcc, PC.fcc
  - TV.240m, PC.240m
  - none, guess
 
  Default is to use the ASS script’s `YCbCr Matrix` or `Video Colorspace` property.

  Recognized .ASS properties: `TV.601`, `TV.709`, `PC.601`, `PC.709`, `TV.240m`, `PC.240m` `TV.fcc`, `PC.fcc` and `none`. 

  `none` and `guess` decides upon on video resolution: width > 1280 or height > 576 → `BT.709`, else → `BT.601`.
  When no hint found in ASS script and `colorspace` parameter is empty then the default is `BT.601`.

### Subtitle

`assrender.Subtitle(clip clip, string[] text, [string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1", int[] start, int[] end, string vfr, int hinting=0, float scale=1.0, float line_spacing=1.0, float dar, float sar, bool set_default_storage_size=True, int top=0, int bottom=0, int left=0, int right=0, string charset, int debuglevel, string fontdir="", string srt_font="sans-serif", string colorspace])`

Like `sub.Subtitle`, it can render single line or multiline subtile string instead of a subtitle file.

- `clip`: Input video clip.

- `text`: String to be rendered, it will rendering multiline string if input a list. These can include ASS Tags.

- `style`: `text` used ASS style

- `start`, `end`: Subtitle display time, start frame number and end frame’s next frame number, it will trim like `[start:end]`, default is all frames of clip.
  - If you input list, each pair of `start` and `end` matches a `text`, and the missing `start` and `end` will be filled with the last one.
  - You must specify both the value of `start` and `end` if use these, otherwise it will be discarded.
  - If you dont’t render multiline subtitles, `std.Loop` maybe faster.

Other parameters are same as `assrender.TextSub`, but not necessarily useful, such as `srt_font`.

### FrameProp

`assrender.FrameProp(clip clip, [string prop="ass", string style="sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1", string vfr, int hinting=0, float scale=1.0, float line_spacing=1.0, float dar, float sar, bool set_default_storage_size=True, int top=0, int bottom=0, int left=0, int right=0, string charset, int debuglevel, string fontdir="", string srt_font="sans-serif", string colorspace])`

`FrameProp` behaves like `Subtitle`, except that the ASS event for a frame comes from the given frame property `prop`, which defaults to `"ass"`. As each frame specifies its own event, there is no need to provide explicit `start`/`end` times like `Subtitle`.

For example, the following code shows the current frame number at the top left corner of each frame for clip c:
```python
c.akarin.Text("FrameNum {N}", prop="ass").assrender.FrameProp()
```

## Csri

It have two csri render names: `assrender_textsub` and `assrender_ob_textsub`. `ob` means old behavior, their differences can be referred to description of `set_default_storage_size` in vapoursynth usage.

## Build instructions

see `.github/workflows/build.yml`

### Standalone libass package (Windows x64)

The dedicated **libass Windows** workflow produces a versioned libass archive with all non-system dependencies and the MSVC runtime linked statically. Its public layout is:

```text
include/ass/ass.h
include/ass/ass_types.h
bin/ass.dll
```

`LICENSES/` and `VERSIONS.txt` are included for redistribution and traceability. Dependency versions are shared by the assrender and standalone builds in `.github/dependency-versions.env`.

The **libass Windows** workflow reuses one `libass-<version>` prerelease tag per libass version. Each run keeps a separate asset named with its assrender commit and UTC build timestamp.

## Licenses
For all modules: see msvc/licenses

## Links
* Doom9 forum: https://forum.doom9.org/showthread.php?t=148926
* Avisynth wiki: http://avisynth.nl/index.php/AssRender
* libass original: https://github.com/libass/libass

## Change log
See https://github.com/AmusementClub/assrender/releases
