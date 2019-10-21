#include "layout.h"
#include "../coding/coding.h"
#include "../vgmstream.h"

/* parse a CD-XA raw mode2/form2 sector */
void block_update_xa(off_t block_offset, VGMSTREAM * vgmstream) {
    STREAMFILE* streamFile = vgmstream->ch[0].streamfile;
    int i, is_audio;
    size_t block_samples;
    uint8_t xa_target, xa_submode, config_target;


    /* XA mode2/form2 sector, size 0x930
     * 0x00: sync word
     * 0x0c: header = minute, second, sector, mode (always 0x02)
     * 0x10: subheader = file, channel, submode flags, xa header
     * 0x14: subheader again (for error correction)
     * 0x18: data
     * 0x918: unused
     * 0x92c: EDC/checksum or null
     * 0x930: end
     * Sectors with no data may exist near other with data
     */
    xa_target = (uint8_t)read_16bitBE(block_offset + 0x10, streamFile);
    config_target = vgmstream->codec_config;

    /* Sector subheader's file+channel markers are used to interleave streams (music/sfx/voices)
     * by reading one target file+channel while ignoring the rest. This is needed to adjust
     * CD drive spinning <> decoding speed (data is read faster otherwise, so can't have 2
     * sectors of the same channel), Normally N channels = N streams (usually 8/16/32 depending
     * on sample rate/stereo), though channels can be empty or contain video (like 7 or 15 video
     * sectors + 1 audio frame). Normally interleaved channels use with the same file ID, but some
     * games change ID too. Extractors deinterleave and split .xa using file + channel + EOF flags. */


    /* submode flag bits (typical audio value = 0x64 01100100)
     * - 7 (0x80 10000000): end of file (usually at data end, not per subheader's file)
     * - 6 (0x40 01000000): real time mode
     * - 5 (0x20 00100000): sector form (0=form1, 1=form2)
     * - 4 (0x10 00010000): trigger (for application)
     * - 3 (0x08 00001000): data sector
     * - 2 (0x04 00000100): audio sector
     * - 1 (0x02 00000010): video sector
     * - 0 (0x01 00000001): end of audio
     * Empty sectors with no flags may exist interleaved with other with audio/data.
     */
    xa_submode = (uint8_t)read_8bit(block_offset + 0x12,streamFile);

    /* audio sector must set/not set certain flags, as per spec */
    is_audio = !(xa_submode & 0x08) && (xa_submode & 0x04) && !(xa_submode & 0x02);


    if (xa_target != config_target) {
        //;VGM_LOG("XA block: ignored block at %x\n", (uint32_t)block_offset);
        block_samples = 0; /* not a target sector */
    }
    else if (is_audio) {
        if (xa_submode & 0x20) {
            /* form2 audio: size 0x900, 18 frames of size 0x80 with 8 subframes of 28 samples */
            block_samples = (28*8 / vgmstream->channels) * 18;
        }
        else { /* rare, found with empty audio [Glint Glitters (PS1), Dance! Dance! Dance! (PS1)] */
            /* form1 audio: size 0x800, 16 frames of size 0x80 with 8 subframes of 28 samples (rest is garbage/other data) */
            block_samples = (28*8 / vgmstream->channels) * 16;
        }
    }
    else {
        ;VGM_ASSERT_ONCE(block_offset < get_streamfile_size(streamFile),
                "XA block: non audio block found at %x\n", (uint32_t)block_offset);
        block_samples = 0; /* not an audio sector */
    }

    vgmstream->current_block_offset = block_offset;
    vgmstream->current_block_samples = block_samples;
    vgmstream->next_block_offset = block_offset + 0x930;

    for (i = 0; i < vgmstream->channels; i++) {
        vgmstream->ch[i].offset = block_offset + 0x18;
    }
}
