#pragma once
#include "foundation/basic.h"
#include "entity_type.h"

struct entity_ctx_o;

enum {
    // Pieces
    PIECE_PAWN = 0x1,
    PIECE_KNIGHT = 0x2,
    PIECE_KING = 0x3,
    PIECE_BISHOP = 0x5,
    PIECE_ROOK = 0x6,
    PIECE_QUEEN = 0x7,
    // Colors
    PIECE_WHITE = 0x0,
    PIECE_BLACK = 0x8,
};

enum {
    // Bitmasks
    MASK_COLOR = 0x8,
    MASK_TYPE = 0x7,
    MASK_SLIDE = 0x4,
    MASK_ROW = 0x70,
};

void create_board(struct entity_ctx_o *ctx, vec3_t world_offset);

void on_entity_pressed(struct entity_ctx_o *ctx, entity_t e);

void update_pieces(struct entity_ctx_o *ctx, float dt);
void update_tiles(struct entity_ctx_o *ctx, float dt);

void draw_board_ui(struct entity_ctx_o *ctx);

static inline const char *get_piece_name(int piece_mask)
{
    switch (piece_mask & MASK_TYPE) {
        case PIECE_PAWN: return "Pawn";
        case PIECE_KNIGHT: return "Knight";
        case PIECE_KING: return "King";
        case PIECE_BISHOP: return "Bishop";
        case PIECE_ROOK: return "Rook";
        case PIECE_QUEEN: return "Queen";
        default: return "Undefined";
    }
}
