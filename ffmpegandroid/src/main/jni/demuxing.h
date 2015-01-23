#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include "log.h"

int demuxing (const char *input, const char *output_v, const char *output_a);