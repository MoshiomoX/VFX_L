/* miniaudio 0.11.25 + stb_vorbis（Ogg Vorbis の読み込み）の実装をここで 1 回だけ作る。
   出典: https://github.com/mackron/miniaudio （Public Domain / MIT-0、LICENSE 参照）
   stb_vorbis は miniaudio の extras/stb_vorbis.c（Public Domain / MIT）。
   順番は miniaudio の説明どおり: stb_vorbis の宣言 → miniaudio の実装 → stb_vorbis の実装 */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
