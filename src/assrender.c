#include "assrender.h"
#include "render.h"
#include "sub.h"
#include "timecodes.h"

static char* read_file_bytes(FILE* fp, size_t* bufsize)
{
    int res;
    long sz;
    long bytes_read;
    char* buf;
    res = fseek(fp, 0, SEEK_END);
    if (res == -1) {
        fclose(fp);
        return 0;
    }

    sz = ftell(fp);
    rewind(fp);

    buf = sz < SIZE_MAX ? malloc(sz + 1) : NULL;
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    bytes_read = 0;
    do {
        res = fread(buf + bytes_read, 1, sz - bytes_read, fp);
        if (res <= 0) {
            fclose(fp);
            free(buf);
            return 0;
        }
        bytes_read += res;
    } while (sz - bytes_read > 0);
    buf[sz] = '\0';
    fclose(fp);

    if (bufsize)
        *bufsize = sz;
    return buf;
}

static const char* detect_bom(const char* buf, const size_t bufsize) {
    if (bufsize >= 4) {
        if (!strncmp(buf, "\xef\xbb\xbf", 3))
            return "UTF-8";
        if (!strncmp(buf, "\x00\x00\xfe\xff", 4))
            return "UTF-32BE";
        if (!strncmp(buf, "\xff\xfe\x00\x00", 4))
            return "UTF-32LE";
        if (!strncmp(buf, "\xfe\xff", 2))
            return "UTF-16BE";
        if (!strncmp(buf, "\xff\xfe", 2))
            return "UTF-16LE";
    }
    return "UTF-8";
}

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <windows.h>
static wchar_t* utf8_to_utf16le(const char* data) {
    const int out_size = MultiByteToWideChar(CP_UTF8, 0, data, -1, NULL, 0);
    wchar_t* out = malloc(out_size * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, data, -1, out, out_size);
    return out;
}
#endif

static FILE* open_utf8_filename(const char* f, const char* m)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
    wchar_t* file_name = utf8_to_utf16le(f);
    wchar_t* mode = utf8_to_utf16le(m);
    FILE* fp = _wfopen(file_name, mode);
    free(file_name);
    free(mode);
    return fp;
#else
    return fopen(f, m);
#endif
}

static char* strrepl(const char* in, const char* str, const char* repl)
{
    size_t siz;
    char* res, * outptr;
    const char* inptr;
    int count = 0;

    inptr = in;

    while ((inptr = strstr(inptr, str))) {
        count++;
        inptr++;
    }

    if (count == 0) {
        size_t in_len = strlen(in);
        res = malloc(in_len + 1);
        memcpy(res, in, in_len + 1);
        return res;
    }

    siz = (strlen(in) - strlen(str) * count + strlen(repl) * count) *
        sizeof(char) + 1;

#define VSMAX(a,b) ((a) > (b) ? (a) : (b))
    res = malloc(VSMAX(siz, strlen(in) * sizeof(char) + 1));
#undef VSMAX

    strcpy(res, in);

    outptr = res;
    inptr = in;

    while ((outptr = strstr(outptr, str)) && (inptr = strstr(inptr, str))) {
        outptr[0] = '\0';
        strcat(outptr, repl);
        strcat(outptr, (inptr + strlen(str)));

        outptr++;
        inptr++;
    }

    return res;
}
static bool frameToTime(int frame, int64_t fpsNum, int64_t fpsDen, char* str, size_t str_size)
{
    int64_t timeint, time_ms;
    time_t time_secs;
    struct tm* ti;
    if (frame != 0 && (INT64_MAX / fpsDen) < ((int64_t)frame * 100)) {
        //Overflow would occur
        return false;
    }
    else {
        timeint = (int64_t)frame * 100 * fpsDen / fpsNum;
    }
    time_secs = timeint / 100;
    time_ms = timeint % 100;
    ti = gmtime(&time_secs);
    snprintf(str, str_size, "%d:%02d:%02d.%02" PRIu64, ti->tm_hour, ti->tm_min, ti->tm_sec, time_ms);

    return true;
}

void VS_CC assrender_destroy_vs(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    const VS_FilterInfo* d = instanceData;
    udata* ud = d->user_data;

    ass_renderer_done(((udata*)ud)->ass_renderer);
    ass_library_done(((udata*)ud)->ass_library);
    ass_free_track(((udata*)ud)->ass);

    free(((udata*)ud)->sub_img[0]);
    free(((udata*)ud)->sub_img[1]);
    free(((udata*)ud)->sub_img[2]);
    free(((udata*)ud)->sub_img[3]);

    if (((udata*)ud)->isvfr)
        free(((udata*)ud)->timestamp);

    free(ud);

    vsapi->freeNode(d->node);
    free(d);

}
void VS_CC assrender_init_vs(VSMap* in, VSMap* out, void** instanceData, VSNode* node, VSCore* core, const VSAPI* vsapi) {
    const VS_FilterInfo* d = *instanceData;
    vsapi->setVideoInfo(d->vi, 1, node);
}
void VS_CC assrender_create_vs(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
    VS_FilterInfo* fi = malloc(sizeof(VS_FilterInfo));
    fi->node = vsapi->propGetNode(in, "clip", 0, NULL);
    fi->vi = vsapi->getVideoInfo(fi->node);
    char e[256] = {0};
    int err = 0;

    const char* vfr = vsapi->propGetData(in, "vfr", 0, &err);
    int h = vsapi->propGetInt(in, "hinting", 0, &err);
    double scale = vsapi->propGetFloat(in, "scale", 0, &err);
    if (err) scale = 1.0;
    double line_spacing = vsapi->propGetFloat(in, "line_spacing", 0, &err);
    double dar = vsapi->propGetFloat(in, "dar", 0, &err);
    double sar = vsapi->propGetFloat(in, "sar", 0, &err);
    int top = vsapi->propGetInt(in, "top", 0, &err);
    int bottom = vsapi->propGetInt(in, "bottom", 0, &err);
    int left = vsapi->propGetInt(in, "left", 0, &err);
    int right = vsapi->propGetInt(in, "right", 0, &err);
    const char* cs = vsapi->propGetData(in, "charset", 0, &err);
    if (err) cs = NULL;
    int debuglevel = vsapi->propGetInt(in, "debuglevel", 0, &err);
    const char* fontdir = vsapi->propGetData(in, "fontdir", 0, &err);
    if (err) fontdir = "";
    const char* srt_font = vsapi->propGetData(in, "srt_font", 0, &err);
    if (err) srt_font = "sans-serif";
    const char* colorspace = vsapi->propGetData(in, "colorspace", 0, &err);
    if (err) colorspace = "";

    char* tmpcsp = calloc(1, BUFSIZ);
    strncpy(tmpcsp, colorspace, BUFSIZ - 1);

    ASS_Hinting hinting;
    udata* data;
    ASS_Track* ass;

    /*
    no unsupported colorspace left, bitness is checked at other place
    if (0 == 1) {
        v = avs_new_value_error(
                "AssRender: unsupported colorspace");
        avs_release_clip(c);
        return v;
    }
    */

    switch (h) {
    case 0:
        hinting = ASS_HINTING_NONE;
        break;
    case 1:
        hinting = ASS_HINTING_LIGHT;
        break;
    case 2:
        hinting = ASS_HINTING_NORMAL;
        break;
    case 3:
        hinting = ASS_HINTING_NATIVE;
        break;
    default:
        vsapi->setError(out, "AssRender: invalid hinting mode");
        return;
    }

    data = malloc(sizeof(udata));

    if (!init_ass(fi->vi->width, fi->vi->height, scale, line_spacing,
        hinting, dar, sar, top, bottom, left, right,
        debuglevel, fontdir, data)) {
        vsapi->setError(out, "AssRender: failed to initialize");
        return;
    }

    if (!strcmp(userData, "TextSub")) {
        const char* f = vsapi->propGetData(in, "file", 0, &err);
        if (!f) {
            vsapi->setError(out, "AssRender: no input file specified");
            return;
        }
        if (!strcasecmp(strrchr(f, '.'), ".srt")) {
            FILE* fp = open_utf8_filename(f, "r");
            ass = parse_srt(fp, data, srt_font);
        }
        else {
            FILE* fp = open_utf8_filename(f, "rb");
            size_t bufsize;
            char* buf = read_file_bytes(fp, &bufsize);
            if (cs == NULL) cs = detect_bom(buf, bufsize);
            ass = ass_read_memory(data->ass_library, buf, bufsize, (char*)cs);
            fp = open_utf8_filename(f, "r");
            ass_read_matrix(fp, tmpcsp);
        }
    }
    else {// if (!strcmp(userData, "Subtitle")){
#define BUFFER_SIZE 16
        int ntext = vsapi->propNumElements(in, "text");
        if (ntext < 1) {
            vsapi->setError(out, "AssRender: No text to be rendered");
            return;
        }
        
        char **texts = malloc(ntext * sizeof(char *));
        int *text_lengths = malloc(ntext * sizeof(int));
        for (int i = 0; i < ntext; i++) {
            texts[i] = vsapi->propGetData(in, "text", i, &err);
            if (err) texts[i] = "";
            texts[i] = strrepl(texts[i], "\n", "\\N");
            text_lengths[i] = strlen(texts[i]);
        }

        const char *style = vsapi->propGetData(in, "style", 0, &err);
        if (err) style = "sans-serif,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1";

        int *startframes = malloc(ntext * sizeof(int));
        int *endframes = malloc(ntext * sizeof(int));
        int nstart = vsapi->propNumElements(in, "start");
        int nend = vsapi->propNumElements(in, "end");
        int nspan = nstart < nend ? nstart : nend;
        if (nspan < 1) {
            for (int i = 0; i < ntext; i++)
                startframes[i] = 0, endframes[i] = fi->vi->numFrames;
        }
        else {
            for (int i = 0; i < nspan; i++) {
                startframes[i] = vsapi->propGetInt(in, "start", i, &err);
                if (err) startframes[i] = 0;
                endframes[i] = vsapi->propGetInt(in, "end", i, &err);
                if (err) endframes[i] = fi->vi->numFrames;
            }
            for (int i = nspan; i < ntext; ++i) {
                startframes[i] = startframes[nspan - 1];
                endframes[i] = endframes[nspan - 1];
            }
        }

        int full_dialogues_length = 0;
        for (int i = 0; i < ntext; i++) {
            char start[BUFFER_SIZE] = { 0 }, end[BUFFER_SIZE] = { 0 };
            if (!frameToTime(startframes[i], fi->vi->fpsNum, fi->vi->fpsDen, start, BUFFER_SIZE) ||
                !frameToTime(endframes[i], fi->vi->fpsNum, fi->vi->fpsDen, end, BUFFER_SIZE)) {
                snprintf(e, 256, "AssRender: Unable to calculate %s time", start[0] ? "end" : "start");
                goto clean;
            }
            int dialogue_space = 32 + strlen(start) + strlen(end) + text_lengths[i];
            char *tmp = malloc(dialogue_space);
            snprintf(tmp, dialogue_space, "Dialogue: 0,%s,%s,Default,,0,0,0,,%s\n", start, end, texts[i]);
            free(texts[i]);
            texts[i] = tmp;
            text_lengths[i] = dialogue_space - 1;
            full_dialogues_length += text_lengths[i];
        }

        char x[BUFFER_SIZE], y[BUFFER_SIZE];
        const char *fmt = "[Script Info]\n"
            "ScriptType: v4.00+\n"
            "PlayResX: %s\n"
            "PlayResY: %s\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,%s\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

        snprintf(x, BUFFER_SIZE, "%d", fi->vi->width);
        snprintf(y, BUFFER_SIZE, "%d", fi->vi->height);

        size_t siz = (412 + strlen(x) + strlen(y) + strlen(style) + full_dialogues_length) * sizeof(char);

        char *final_text = malloc(siz);
        snprintf(final_text, siz, fmt, x, y, style);

        int pos = strlen(final_text);
        for (int i = 0; i < ntext; i++) {
            memcpy(final_text + pos, texts[i], text_lengths[i]);
            pos += text_lengths[i];
        }
        final_text[pos] = 0;

        ass = ass_read_memory(data->ass_library, final_text, pos, "UTF-8");

        free(final_text);

    clean:
        free(startframes);
        free(endframes);
        free(text_lengths);
        for (int i = 0; i < ntext; i++)
            free(texts[i]);
        free(texts);

        if (*e)
            vsapi->setError(out, e);

#undef BUFFER_SIZE
    }

    if (!ass) {
        vsapi->setError(out, "AssRender: unable to parse ass text");
        return;
    }

    data->ass = ass;

    if (vfr) {
        int ver;
        FILE* fh = open_utf8_filename(vfr, "r");

        if (!fh) {
            snprintf(e, 256, "AssRender: could not read timecodes file '%s'", vfr);
            vsapi->setError(out, e);
            return;
        }

        data->isvfr = 1;

        if (fscanf(fh, "# timecode format v%d", &ver) != 1) {
            snprintf(e, 256, "AssRender: invalid timecodes file '%s'", vfr);
            vsapi->setError(out, e);
            return;
        }

        switch (ver) {
        case 1:

            if (!parse_timecodesv1(fh, fi->vi->numFrames, data)) {
                vsapi->setError(out, "AssRender: error parsing timecodes file");
                return;
            }

            break;
        case 2:

            if (!parse_timecodesv2(fh, fi->vi->numFrames, data)) {
                vsapi->setError(out, "AssRender: timecodes file had less frames than expected");
                return;
            }

            break;
        }

        fclose(fh);
    }
    else {
        data->isvfr = 0;
    }

    matrix_type color_mt;

    if (fi->vi->format->colorFamily == cmRGB) {
        color_mt = MATRIX_NONE; // no RGB->YUV conversion
    }
    else {
        // .ASS "YCbCr Matrix" valid values are
        // "none" "tv.601" "pc.601" "tv.709" "pc.709" "tv.240m" "pc.240m" "tv.fcc" "pc.fcc"
        if (!strcasecmp(tmpcsp, "bt.709") || !strcasecmp(tmpcsp, "rec709") || !strcasecmp(tmpcsp, "tv.709")) {
            color_mt = MATRIX_BT709;
        }
        else if (!strcasecmp(tmpcsp, "pc.709")) {
            color_mt = MATRIX_PC709;
        }
        else if (!strcasecmp(tmpcsp, "bt.601") || !strcasecmp(tmpcsp, "rec601") || !strcasecmp(tmpcsp, "tv.601")) {
            color_mt = MATRIX_BT601;
        }
        else if (!strcasecmp(tmpcsp, "pc.601")) {
            color_mt = MATRIX_PC601;
        }
        else if (!strcasecmp(tmpcsp, "tv.fcc")) {
            color_mt = MATRIX_TVFCC;
        }
        else if (!strcasecmp(tmpcsp, "pc.fcc")) {
            color_mt = MATRIX_PCFCC;
        }
        else if (!strcasecmp(tmpcsp, "tv.240m")) {
            color_mt = MATRIX_TV240M;
        }
        else if (!strcasecmp(tmpcsp, "pc.240m")) {
            color_mt = MATRIX_PC240M;
        }
        else if (!strcasecmp(tmpcsp, "bt.2020") || !strcasecmp(tmpcsp, "rec2020")) {
            color_mt = MATRIX_BT2020;
        }
        else if (!strcasecmp(tmpcsp, "none") || !strcasecmp(tmpcsp, "guess")) {
            /* not yet
            * Theoretically only for 10 and 12 bits:
            if (fi->vi.width > 1920 || fi->vi.height > 1080)
              color_mt = MATRIX_BT2020;
            else
            */
            if (fi->vi->width > 1280 || fi->vi->height > 576)
                color_mt = MATRIX_PC709;
            else
                color_mt = MATRIX_PC601;
        }
        else {
            color_mt = MATRIX_BT601;
        }
    }

    FillMatrix(&data->mx, color_mt);

    const int bits_per_pixel = fi->vi->format->bitsPerSample;
    const int pixelsize = fi->vi->format->bytesPerSample;
    const int greyscale = fi->vi->format->colorFamily == cmGray;

    if (bits_per_pixel == 8)
        data->f_make_sub_img = make_sub_img;
    else if (bits_per_pixel <= 16)
        data->f_make_sub_img = make_sub_img16;
    else {
        vsapi->setError(out, "AssRender: unsupported bit depth: 32");
        return;
    }


    switch (fi->vi->format->id)
    {
    case pfYUV420P8:
        data->apply = apply_yv12;
        break;
    case pfYUV420P10:
    case pfYUV420P12:
    case pfYUV420P14:
    case pfYUV420P16:
        data->apply = apply_yuv420;
        break;
    case pfYUV422P8:
        data->apply = apply_yv16;
        break;
    case pfYUV422P10:
    case pfYUV422P12:
    case pfYUV422P14:
    case pfYUV422P16:
        data->apply = apply_yuv422;
        break;
    case pfYUV444P8:
    case pfRGB24:
        data->apply = apply_yv24;
        break;
    case pfYUV444P10:
    case pfYUV444P12:
    case pfYUV444P14:
    case pfYUV444P16:
    case pfRGB48:
        data->apply = apply_yuv444;
        break;
    case pfGray8:
        data->apply = apply_y8;
        break;
    case pfGray16:
        data->apply = apply_y;
        break;
    default:
        vsapi->setError(out, "AssRender: unsupported pixel type");
        return;
    }

    free(tmpcsp);

    const int buffersize = fi->vi->width * fi->vi->height * pixelsize;

    data->sub_img[0] = malloc(buffersize);
    data->sub_img[1] = malloc(buffersize);
    data->sub_img[2] = malloc(buffersize);
    data->sub_img[3] = malloc(buffersize);

    data->bits_per_pixel = bits_per_pixel;
    data->pixelsize = pixelsize;
    data->rgb_fullscale = fi->vi->format->colorFamily == cmRGB;
    data->greyscale = greyscale;

    fi->user_data = data;

    vsapi->createFilter(in, out, userData, assrender_init_vs, assrender_get_frame_vs, assrender_destroy_vs, fmParallelRequests, 0, fi, core);

    return;
}
#define COMMON_PARAMS \
        "vfr:data:opt;" \
        "hinting:int:opt;" \
        "scale:float:opt;" \
        "line_spacing:float:opt;" \
        "dar:float:opt;" \
        "sar:float:opt;" \
        "top:int:opt;" \
        "bottom:int:opt;" \
        "left:int:opt;" \
        "right:int:opt;" \
        "charset:data:opt;" \
        "debuglevel:int:opt;" \
        "fontdir:data:opt;" \
        "srt_font:data:opt;" \
        "colorspace:data:opt;",
void VS_CC VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin) {
    configFunc("com.pinterf.assrender", "assrender", "AssRender", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("TextSub",
        "clip:clip;"
        "file:data;"
        COMMON_PARAMS
        assrender_create_vs, "TextSub", plugin);
    registerFunc("Subtitle",
        "clip:clip;"
        "text:data[];"
        "style:data:opt;"
        "start:int[]:opt;"
        "end:int[]:opt;"
        COMMON_PARAMS
        assrender_create_vs, "Subtitle", plugin);
}