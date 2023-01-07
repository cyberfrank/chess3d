#pragma once
#include "foundation/basic.h"
#include "render/material.h"
#include "entity_type.h"

struct entity_ctx_o;

// Component handles
uint32_t transform_id;
uint32_t light_id;
uint32_t volume_id;
uint32_t mesh_id;
uint32_t piece_id;
uint32_t tile_id;
uint32_t board_id;

enum {
    LIGHT_TYPE_POINT,
    LIGHT_TYPE_SPOT,
    LIGHT_TYPE_DIRECTIONAL,
    LIGHT_TYPE_IBL,
};

typedef struct light_component_t {   
    bool enabled;
    uint32_t type;
    vec3_t color;
    float intensity;
    // Spot light params
    float spot_angle_inner;
    float spot_angle_outer;
    // Resource ids for IBL light
    gfx_id ibl_diffuse;
    gfx_id ibl_specular;
} light_component_t;

typedef struct volume_component_t {
    vec3_t bb_min;
    vec3_t bb_max;
    float blend_distance;
} volume_component_t;

enum {
    MAX_NUM_MATERIALS = 16,
};

typedef struct mesh_component_t {
    char mesh_path[64];
    uint32_t num_materials;
    char material_path[MAX_NUM_MATERIALS][64];
    uint64_t visibility_mask;
    struct mesh_t *data;
    struct material_t materials[MAX_NUM_MATERIALS];
} mesh_component_t;

typedef struct piece_component_t {
    // Piece color and type mask
    uint8_t mask;
    // The board that this piece is part of
    entity_t board;
    // Position on board
    int board_position;
    // True if the piece should animate movement
    // between `world_pos_from` and `world_pos_true`
    bool want_to_move;
    float move_t;
    vec3_t world_pos_from;
    vec3_t world_pos_to;
} piece_component_t;

typedef struct tile_component_t {
    uint8_t x;
    uint8_t z;
    entity_t board;
} tile_component_t;

typedef struct board_component_t {
    entity_t selected_piece;
    bool legal_move_indices[64];
    uint8_t indices[64 * 2];
    uint8_t current_player;
    uint8_t castle_bits;
    uint8_t num_white_captures;
    uint8_t num_black_captures;
    int en_passant_pos;
    uint32_t move_count;
    // Non-zero if game is over (win/draw)
    uint8_t game_state;
} board_component_t;

void register_all_components(struct entity_ctx_o *ctx);

static inline void set_mesh_path(mesh_component_t *c, const char *path)
{
    snprintf(c->mesh_path, 64, "%s", path);
}

static inline void set_material_path(mesh_component_t *c, const char *path, uint32_t idx)
{
    snprintf(c->material_path[idx], 64, "%s", path);
    if (idx >= c->num_materials)
        c->num_materials = idx + 1;
}