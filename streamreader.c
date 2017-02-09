//
//  streamreader.c
//  deadbeef
//
//  Created by Oleksiy Yakovenko on 09/02/2017.
//  Copyright © 2017 Alexey Yakovenko. All rights reserved.
//

#include <string.h>
#include "streamreader.h"
#include "replaygain.h"

#define BLOCK_SIZE 16384
#define BLOCK_COUNT 16

static streamblock_t *blocks; // list of all blocks

static streamblock_t *block_data; // first available block with data (can be NULL)

static streamblock_t *block_next; // next block available

void
streamreader_init (void) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        streamblock_t *b = calloc (1, sizeof (streamblock_t));
        b->pos = -1;
        b->buf = malloc (BLOCK_SIZE);
        b->next = blocks;
        blocks = b;
    }
    block_next = blocks;
}

void
streamreader_free (void) {
    while (blocks) {
        streamblock_t *next = blocks->next;
        free (blocks->buf);
        free (blocks);
        blocks = next;
    }
    block_next = block_data = NULL;
}

int
streamreader_read_next_block (playItem_t *track, DB_fileinfo_t *fileinfo, streamblock_t **block) {
    *block = NULL;
    if (block_next->pos >= 0) {
        return 0; // all buffers full
    }

    // clip size to max possible, with current sample format
    int size = BLOCK_SIZE;
    int samplesize = fileinfo->fmt.channels * (fileinfo->fmt.bps>>3);
    int mod = size % samplesize;
    if (mod) {
        size -= mod;
    }

    int rb = fileinfo->plugin->read (fileinfo, block_next->buf, size);
    if (rb < 0) {
        return -1;
    }

    block_next->pos = 0;
    block_next->size = rb;
    memcpy (&block_next->fmt, &fileinfo->fmt, sizeof (ddb_waveformat_t));
    pl_item_ref (track);
    block_next->track = track;

    int input_does_rg = fileinfo->plugin->plugin.flags & DDB_PLUGIN_FLAG_REPLAYGAIN;
    if (!input_does_rg) {
        ddb_replaygain_settings_t rg_settings;
        rg_settings._size = sizeof (rg_settings);
        replaygain_init_settings (&rg_settings, track);
        replaygain_set_current (&rg_settings);
        replaygain_apply (&fileinfo->fmt, block_next->buf, block_next->size);
    }


    block_next->last = (rb != size);

    if (!block_data) {
        block_data = block_next;
    }

    *block = block_next;

    block_next = block_next->next;
    if (!block_next) {
        block_next = blocks;
    }

    return 1;
}

streamblock_t *
streamreader_get_curr_block (void) {
    return block_data;
}

void
streamreader_next_block (void) {
    if (block_data) {
        block_data->pos = -1;
        pl_item_unref (block_data->track);
        block_data->track = NULL;

        block_data = block_data->next;
        if (!block_data) {
            block_data = blocks;
        }
    }

    if (block_data && block_data->pos < 0) {
        block_data = NULL; // no available blocks with data
    }
}

void
streamreader_reset (void) {
    streamblock_t *b = blocks;
    while (b) {
        if (b->pos >= 0) {
            b->pos = -1;
            pl_item_unref (b->track);
        }
        b = b->next;
    }
    block_next = blocks;
    block_data = NULL;
}
