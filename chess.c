#include "chess.h"
#include "foundation/math.h"
#include "foundation/log.h"
#include "foundation/random.h"
#include "foundation/rect.h"
#include "render/mesh.h"
#include "render/window.h"
#include "render/visibility_mask.h"
#include "im2d.h"
#include "debug_draw.h"
#include "entity.h"
#include "components.h"

static const float grid_size = 4.315f;

enum {
    MOVE_TYPE_MOVE,
    MOVE_TYPE_CAPTURE,
    MOVE_TYPE_CASTLE,
};

enum {
    STATE_PLAYING,
    STATE_WHITE_WIN_BY_CHECKMATE,
    STATE_BLACK_WIN_BY_CHECKMATE,
    STATE_DRAW_BY_STALEMATE,
};

typedef struct move_info_t {
    uint8_t move_type;
    uint8_t capture;
    int capture_pos;
    int rook_pos; // Castling
    uint8_t last_castle_bits;
    int last_en_passant_pos;
    uint8_t promotion;
} move_info_t;

static inline vec3_t grid_to_world_pos(int x, int z, vec3_t offset)
{
    float x_pos = (x - 4) * grid_size + grid_size * 0.5f;
    float z_pos = (z - 4) * grid_size + grid_size * 0.5f;
    return vec3_add(offset, (vec3_t) { x_pos, 0, z_pos });
}

static move_info_t perform_move(board_component_t *board, int from, int to)
{
    uint8_t capture = board->indices[to];

    move_info_t move_info = {
        .move_type = capture ? MOVE_TYPE_CAPTURE : MOVE_TYPE_MOVE,
        .capture = capture,
        .capture_pos = to,
        .last_castle_bits = board->castle_bits,
        .last_en_passant_pos = board->en_passant_pos,
        .promotion = 0,
    };

    if ((board->indices[from] & MASK_TYPE) == PIECE_KING) {
        // King was moved; remove castling rights for both sides
        board->castle_bits &= ~(3 << (board->current_player / 4));
        // Castling happened; find which side and move the rook
        if (abs(from - to) == 2) {
            int rook_from = from + (from > to ? -3 : 4);
            int rook_to = from + (from > to ? -1 : 1);
            board->indices[rook_to] = board->indices[rook_from];
            board->indices[rook_from] = 0;
            move_info.move_type = MOVE_TYPE_CASTLE;
            move_info.rook_pos = rook_from;
        }
    }

    if ((board->indices[from] & MASK_TYPE) == PIECE_ROOK) {
        // Revoke castling rights for own side
        if (from == 0x0 || from == 0x70)
            board->castle_bits &= ~(1 << (board->current_player / 4 + 0));
        else if (from == 0x7 || from == 0x77)
            board->castle_bits &= ~(1 << (board->current_player / 4 + 1));
    }

    if ((board->indices[to] & MASK_TYPE) == PIECE_ROOK) {
        // Revoke castling rights for opponent side
        uint8_t opponent = board->current_player == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE;
        if (to == 0x0 || to == 0x70)
            board->castle_bits &= ~(1 << (opponent / 4 + 0));
        else if (to == 0x7 || to == 0x77)
            board->castle_bits &= ~(1 << (opponent / 4 + 1));
    }

    if ((board->indices[from] & MASK_TYPE) == PIECE_PAWN) {
        int diff = abs(from - to);
        if (diff == 32) {
            // Moved two squares ahead
            board->en_passant_pos = to;
        }
        else if ((diff == 17 || diff == 15) && capture == 0) {
            // En passant move
            move_info.move_type = MOVE_TYPE_CAPTURE;
            int pos = board->en_passant_pos;
            move_info.capture_pos = pos;
            move_info.capture = board->indices[pos];
            board->indices[pos] = 0;
            board->en_passant_pos = 0;
        }
        else {
            board->en_passant_pos = 0;
        }
    } else {
        board->en_passant_pos = 0;
    }

    if ((board->indices[from] & MASK_TYPE) == PIECE_PAWN) {
        int row = to & MASK_ROW;
        if (row == 0x00 || row == 0x70) {
            const uint8_t piece_type = PIECE_QUEEN;
            board->indices[from] = PIECE_QUEEN | (board->indices[from] & MASK_COLOR);
            move_info.promotion = piece_type;
        }
    }

    board->indices[to] = board->indices[from];
    board->indices[from] = 0;

    board->current_player = board->current_player == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE;
    ++board->move_count;

    return move_info;
}

static void revert_move(board_component_t *board, int from, int to, const move_info_t *info)
{   
    board->indices[from] = board->indices[to];
    // Set to zero in case the `capture_pos` was != `to`
    board->indices[to] = 0;
    board->indices[info->capture_pos] = info->capture;

    board->castle_bits = info->last_castle_bits;
    board->en_passant_pos = info->last_en_passant_pos;
    
    // Revert castling rook move
    if (info->move_type == MOVE_TYPE_CASTLE) {
        int rook_from = from + (from > to ? -3 : 4);
        int rook_to = from + (from > to ? -1 : 1);
        board->indices[rook_from] = board->indices[rook_to];
        board->indices[rook_to] = 0;
    }

    // Revert promotion
    if (info->promotion) {
        board->indices[from] = PIECE_PAWN | (board->indices[from] & MASK_COLOR);
    }

    board->current_player = board->current_player == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE;
    --board->move_count;
}

static bool is_legal_move(board_component_t *board, int from, int to)
{
    if ((to & 0x88) != 0)
        return false;

    uint8_t piece_to_move = board->indices[from];
    if (piece_to_move == 0)
        return false;

    if ((piece_to_move & MASK_COLOR) != board->current_player)
        return false;

    uint8_t piece_to_capture = board->indices[to];
    if (piece_to_capture != 0 && (piece_to_capture & MASK_COLOR) == board->current_player)
        return false;

    bool can_move = false;

    int diff = abs(from - to);
    switch (piece_to_move & MASK_TYPE) {
        case PIECE_PAWN: {
            int dir = from - to > 0 ? 0 : 8;
            int row = from & MASK_ROW;
            if ((piece_to_move & MASK_COLOR) == dir) {
                can_move |= (diff == 16 && piece_to_capture == 0);
                can_move |= ((diff == 15 || diff == 17) && piece_to_capture != 0);
                can_move |= (diff == 32 && (row == 0x60 || row == 0x10) && piece_to_capture == 0 && board->indices[from + (dir != 0 ? 16 : -16)] == 0);
                if (board->en_passant_pos && piece_to_capture == 0) {
                    can_move |= (diff == (dir != 0 ? 15 : 17) && (from - 1) == board->en_passant_pos);
                    can_move |= (diff == (dir != 0 ? 17 : 15) && (from + 1) == board->en_passant_pos);
                }
            }
            break;
        }
        case PIECE_KNIGHT: {
            can_move |= (diff == 14 || diff == 18 || diff == 31 || diff == 33);
            break;
        }
        case PIECE_KING: {
            int dir = from - to > 0 ? 1 : 0;
            // Castling move; check castling rights and check if rook move is legal
            can_move |= (diff == 2 && (board->castle_bits >> (board->current_player / 4 + dir) & 1) != 0 && is_legal_move(board, from + (dir != 0 ? -3 : 4), from + (dir != 0 ? -1 : 1)));
            can_move |= (diff == 1 || diff == 16 || diff == 17 || diff == 15);
            break;
        }
        case PIECE_BISHOP: {
            can_move |= (diff % 15 == 0 || diff % 17 == 0);
            break;
        }
        case PIECE_ROOK: {
            can_move |= ((from & 0x0f) == (to & 0x0f) || (from & 0xf0) == (to & 0xf0));
            break;
        }
        case PIECE_QUEEN: {
            can_move |= (diff % 15 == 0 || diff % 17 == 0 || (from & 0x0f) == (to & 0x0f) || (from & 0xf0) == (to & 0xf0));
            break;
        }
    }

    if (can_move && (piece_to_move & MASK_SLIDE)) {
        int dir = to - from;
        int step = 0;
        if (dir % 17 == 0) step = 17;
        else if (dir % 15 == 0) step = 15;
        else if (dir % 16 == 0) step = 16;
        else step = 1;

        step = (dir / step < 0) ? -step : step;

        int path = from + step;
        for (int i = 1; i < (to - from) / step; ++i, path += step) {
            can_move &= board->indices[path] == 0;
        }
    }

    return can_move;
}

static bool is_piece_attacked(board_component_t *board, uint8_t piece)
{
    // Find piece position
    int pos = 0;
    for (int i = 0; i < 128; ++i) {
        if (board->indices[i] == piece) {
            pos = i;
            break;
        }
    }

    // Check if any piece can attack `pos`
    bool attacked = false;
    for (int i = 0; i < 128; ++i) {
        if (is_legal_move(board, i, pos)) {
            attacked = true;
            break;
        }
    }

    return attacked;
}

static bool is_checked_after_move(board_component_t *board, int from, int to)
{
    move_info_t info = perform_move(board, from, to);

    const uint8_t king_piece = PIECE_KING | (board->current_player == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE);
    bool checked = is_piece_attacked(board, king_piece);
    revert_move(board, from, to, &info);

    return checked;
}

static void check_end_condition_reached(board_component_t *board)
{
    // Switch player temporarily to check if king is checked
    const uint8_t player = board->current_player;
    board->current_player = player == PIECE_WHITE ? PIECE_BLACK : PIECE_WHITE;
    bool checked = is_piece_attacked(board, PIECE_KING | player);
    board->current_player = player;

    int num_legal_moves = 0;
    for (int from = 0; from < 128; ++from)
        for (int to = 0; to < 128; ++to)
            num_legal_moves += (is_legal_move(board, from, to) && !is_checked_after_move(board, from, to));

    if (num_legal_moves == 0) {
        log_print(LOG_INFO, "No legal moves! Stalemate: %i", !checked);
    }

    if (num_legal_moves == 0) {
        if (checked)
            board->game_state = player == PIECE_WHITE ? STATE_BLACK_WIN_BY_CHECKMATE: STATE_WHITE_WIN_BY_CHECKMATE;
        else
            board->game_state = STATE_DRAW_BY_STALEMATE;
    }
    else {
        board->game_state = STATE_PLAYING;
    }
}

static void update_legal_move_indices_for_piece(board_component_t *board, piece_component_t *piece)
{
    int from = piece->board_position;
    for (uint32_t board_idx = 0; board_idx < 64; ++board_idx) {
        int to = (board_idx % 8) + (board_idx / 8) * 16;
        board->legal_move_indices[board_idx] = is_legal_move(board, from, to) && !is_checked_after_move(board, from, to);
    }
}

static entity_t add_piece(entity_t owner, entity_ctx_o *ctx, uint8_t piece_mask, int x, int z, vec3_t offset)
{
    entity_t e = make_entity(ctx);

    transform_t *tm = add_component(ctx, e, transform_id);
    tm->pos = grid_to_world_pos(x, z, offset);

    if ((piece_mask & MASK_TYPE) == PIECE_PAWN || (piece_mask & MASK_TYPE) == PIECE_ROOK) {
        // Set random rotation for pawn and rook
        float angle = random_float(0, PI * 2);
        tm->rot = euler_to_quaternion(make_vec3(0, angle, 0));
    }

    if ((piece_mask & MASK_TYPE) == PIECE_KNIGHT) {
        // Rotate knight 45 degrees depending on side
        float angle = (piece_mask & MASK_COLOR) == PIECE_WHITE ? PI * 0.5f : PI * -0.5f;
        tm->rot = euler_to_quaternion(make_vec3(0, angle, 0));
    }

    piece_component_t *piece = add_component(ctx, e, piece_id);
    piece->mask = piece_mask;
    piece->board = owner;
    piece->board_position = x + z * 16;

    char *mesh_name = 0;
    switch (piece_mask & MASK_TYPE) {
        case PIECE_PAWN:
            mesh_name = temp_print("Pawn_0%i", x + 1);
            break;
        case PIECE_KNIGHT:
            mesh_name = temp_print("Knight_0%i", x > 4 ? 1 : 2);
            break;
        case PIECE_KING:
            mesh_name = "King";
            break;
        case PIECE_BISHOP:
            mesh_name = temp_print("Bishop_0%i", x > 4 ? 1 : 2);
            break;
        case PIECE_ROOK:
            mesh_name = temp_print("Rook_0%i", x > 4 ? 1 : 2);
            break;
        case PIECE_QUEEN:
            mesh_name = "Queen";
            break;
        default:
            mesh_name = "Pawn_01";
            break;
    }

    mesh_component_t *mesh = add_component(ctx, e, mesh_id);
    const char *color_name = (piece_mask & MASK_COLOR) == PIECE_WHITE ? "White" : "Black";
    set_mesh_path(mesh, temp_print("data/models/chess/%s_%s.triangle_mesh", color_name, mesh_name));
    set_material_path(mesh, "data/materials/pieces.material", 0);

    return e;
}

static void add_reflection_probe(entity_ctx_o *ctx, vec3_t pos, float r)
{
    entity_t e = make_entity(ctx);
    transform_t *tm = add_component(ctx, e, transform_id);
    tm->pos = pos;

    light_component_t *light = add_component(ctx, e, light_id);
    light->type = LIGHT_TYPE_IBL;

    volume_component_t *volume = add_component(ctx, e, volume_id);
    volume->bb_min = make_vec3(-r, -r, -r);
    volume->bb_max = make_vec3(r, r, r);
    volume->blend_distance = r * 0.3f;

    mesh_component_t *mesh = add_component(ctx, e, mesh_id);
    set_mesh_path(mesh, "data/models/sphere.triangle_mesh");
    set_material_path(mesh, "data/materials/reflection_probe.material", 0);
    mesh->visibility_mask = VIEWER_MASK_EDITOR;
}

void create_board(entity_ctx_o *ctx, vec3_t offset)
{
    entity_t owner = make_entity(ctx);

    // Ground plane
    transform_t *board_tm = add_component(ctx, owner, transform_id);
    board_tm->pos = offset;
    board_tm->rot = quaternion_from_rotation((vec3_t) { 1, 0, 1 }, PI);
    mesh_component_t *board_mesh = add_component(ctx, owner, mesh_id);
    set_mesh_path(board_mesh, "data/models/chess/Board.triangle_mesh");
    set_material_path(board_mesh, "data/materials/board.material", 0);
    board_mesh->visibility_mask = VIEWER_MASK_MAIN;

    board_component_t *board = add_component(ctx, owner, board_id);
    (void)board;

    for (int j = 0; j < 2; ++j) {
        uint8_t color = j == 0 ? PIECE_WHITE : PIECE_BLACK;
        uint8_t row = j != 0 ? 0 : 7;
        add_piece(owner, ctx, PIECE_ROOK | color, 0, row, offset);
        add_piece(owner, ctx, PIECE_KNIGHT | color, 1, row, offset);
        add_piece(owner, ctx, PIECE_BISHOP | color, 2, row, offset);
        add_piece(owner, ctx, PIECE_KING | color, 3, row, offset);
        add_piece(owner, ctx, PIECE_QUEEN | color, 4, row, offset);
        add_piece(owner, ctx, PIECE_BISHOP | color, 5, row, offset);
        add_piece(owner, ctx, PIECE_KNIGHT | color, 6, row, offset);
        add_piece(owner, ctx, PIECE_ROOK | color, 7, row, offset);
    }

    for (int i = 0; i < 8; ++i) {
        add_piece(owner, ctx, PIECE_PAWN | PIECE_WHITE, i, 6, offset);
        add_piece(owner, ctx, PIECE_PAWN | PIECE_BLACK, i, 1, offset);
    }

    // Create grid overlay
    for (int z = 0; z < 8; ++z) {
        for (int x = 0; x < 8; ++x) {
            entity_t e = make_entity(ctx);
            transform_t *tm = add_component(ctx, e, transform_id);
            tm->pos = grid_to_world_pos(x, z, offset);
            // Avoid clipping board mesh
            tm->pos.y += 0.01f;
            tm->scl = vec3_mul((vec3_t) { 1, 1, 1 }, grid_size * 0.01f * 0.5f);

            mesh_component_t *mesh = add_component(ctx, e, mesh_id);
            set_mesh_path(mesh, "data/models/chess/Plane.triangle_mesh");
            set_material_path(mesh, "data/materials/tile.material", 0);
            mesh->visibility_mask = 0;

            tile_component_t *tile = add_component(ctx, e, tile_id);
            tile->x = (uint8_t)x;
            tile->z = (uint8_t)z;
            tile->board = owner;
        }
    }

    vec3_t probe_offset = make_vec3(offset.x + grid_size * 0.5f, 3.f, offset.z + grid_size * 0.5f);
    add_reflection_probe(ctx, grid_to_world_pos(3, 3, probe_offset), grid_size * 5.f);
}

static void move_piece(entity_ctx_o *ctx, entity_t e, int x, int z, vec3_t board_pos)
{
    piece_component_t *piece = get_component(ctx, e, piece_id);
    transform_t *transform = get_component(ctx, e, transform_id);
    piece->move_t = 0.0f;
    piece->want_to_move = true;
    piece->world_pos_from = transform->pos;
    piece->world_pos_to = grid_to_world_pos(x, z, board_pos);
    piece->board_position = x + z * 16;
}

static void move_piece_offboard(entity_ctx_o *ctx, entity_t e, uint8_t num_captures, vec3_t board_pos)
{
    piece_component_t *piece = get_component(ctx, e, piece_id);
    transform_t *transform = get_component(ctx, e, transform_id);
    piece->move_t = 0.0f;
    piece->want_to_move = true;
    piece->world_pos_from = transform->pos;

    const float piece_size = 2.7f;
    const float x_pos = 4.7f * grid_size;
    float x = (piece->mask & MASK_COLOR) == PIECE_WHITE ? x_pos : -x_pos;
    float z = piece_size * ((num_captures + 1) / 2) * (num_captures % 2 ? 1 : -1);
    piece->world_pos_to = vec3_add(board_pos, make_vec3(x, 0, z));
    piece->board_position = -1;
}

static void try_move_selected_piece(entity_ctx_o *ctx, entity_t board_entity, int x, int z)
{
    board_component_t *board = get_component(ctx, board_entity, board_id);
    transform_t *board_transform = get_component(ctx, board_entity, transform_id);
    entity_t selected = board->selected_piece;

    if (!is_entity_alive(ctx, selected))
        return;

    piece_component_t *piece = get_component(ctx, selected, piece_id);
    int from = piece->board_position;
    int to = x + z * 16;

    if (!(is_legal_move(board, from, to) && !is_checked_after_move(board, from, to)))
        return;

    move_info_t info = perform_move(board, from, to);
    check_end_condition_reached(board);

    if (info.promotion) {
        destroy_entity(ctx, selected);
        selected = add_piece(board_entity, ctx, info.promotion | (piece->mask & MASK_COLOR), from % 16, from / 16, board_transform->pos);
        run_load_callback_for_entity(ctx, selected);
    }

    // Check if we need to move another piece as part of this move
    // This can be either a capture, castling or en passant
    if (info.move_type != MOVE_TYPE_MOVE) {
        int piece_pos = info.move_type == MOVE_TYPE_CAPTURE ? info.capture_pos : info.rook_pos;

        piece_component_t *pieces = component_data(ctx, piece_id);
        entity_t e;
        uint32_t idx = 0;
        const uint64_t mask = 1 << piece_id | 1 << transform_id;
        while (find_next_component(ctx, piece_id, mask, &idx, &e)) {
            if (pieces[idx].board_position == piece_pos && pieces[idx].board.id == board_entity.id) {
                break;
            }
            ++idx;
        }

        if (info.move_type == MOVE_TYPE_CASTLE)
            move_piece(ctx, e, x + (from > to ? 1 : -1), z, board_transform->pos);
        else if (info.move_type == MOVE_TYPE_CAPTURE) {
            bool is_white = (piece->mask & MASK_COLOR) == PIECE_WHITE;
            move_piece_offboard(ctx, e, is_white ? board->num_black_captures : board->num_white_captures, board_transform->pos);

            if (is_white)
                ++board->num_black_captures;
            else
                ++board->num_white_captures;
        }
    }

    // Move piece
    move_piece(ctx, selected, x, z, board_transform->pos);

    board->selected_piece.id = UINT64_MAX;
    memset(board->legal_move_indices, 0, 64);
}

void on_entity_pressed(entity_ctx_o *ctx, entity_t e)
{   
    if (has_component(ctx, e, piece_id)) {
        // Find board that this piece is on
        piece_component_t *piece = get_component(ctx, e, piece_id);
        if (is_entity_alive(ctx, piece->board)) {
            board_component_t *board = get_component(ctx, piece->board, board_id);
            bool is_opponent = (piece->mask & MASK_COLOR) != board->current_player;
            // Wants to capture opponent piece
            if (is_opponent && board->selected_piece.id != UINT64_MAX) {
                int x = piece->board_position % 16;
                int z = piece->board_position / 16;
                try_move_selected_piece(ctx, piece->board, x, z);
            }
            // Change selection
            else if (!is_opponent) {
                board->selected_piece = e;
                update_legal_move_indices_for_piece(board, piece);
            }
        }
    }
    else if (has_component(ctx, e, tile_id)) {
        // Tile was pressed
        // Find the board and try to move the selected piece, if any
        tile_component_t *tile = get_component(ctx, e, tile_id);
        if (is_entity_alive(ctx, tile->board)) {
            try_move_selected_piece(ctx, tile->board, tile->x, tile->z);
        }
    }
}

void update_pieces(entity_ctx_o *ctx, float dt)
{
    piece_component_t *pieces = component_data(ctx, piece_id);
    const uint64_t mask = (1 << piece_id | 1 << transform_id);

    entity_t e;
    uint32_t i = 0;
    while (find_next_component(ctx, piece_id, mask, &i, &e)) {
        piece_component_t *p = &pieces[i];
        if (p->move_t >= 1.0f) {
            p->want_to_move = false;
        } 
        else if (p->want_to_move) {
            transform_t *tm = get_component(ctx, e, transform_id);
            tm->pos = vec3_lerp(p->world_pos_from, p->world_pos_to, p->move_t);

            const float height = 0.6f;
            tm->pos.y += sinf(p->move_t * PI) * height;
            p->move_t += dt;
        }
        ++i;
    }
}

void update_tiles(entity_ctx_o *ctx, float dt)
{
    tile_component_t *tiles = component_data(ctx, tile_id);
    const uint64_t mask = (1 << tile_id | 1 << mesh_id);

    entity_t e;
    uint32_t i = 0;
    while (find_next_component(ctx, tile_id, mask, &i, &e)) {
        tile_component_t tile = tiles[i];

        mesh_component_t *mesh = get_component(ctx, e, mesh_id);
        board_component_t *board = get_component(ctx, tile.board, board_id);
        
        const int idx = tile.x + tile.z * 8;
        mesh->visibility_mask = (board->legal_move_indices[idx] ? VIEWER_MASK_MAIN : 0);

        ++i;
    }
}

void draw_board_ui(struct entity_ctx_o *ctx)
{
    board_component_t *boards = component_data(ctx, board_id);

    extern struct font_t *font_default;

    const uint64_t mask = (1ULL << board_id);
    entity_t e;
    uint32_t i = 0;
    while (find_next_component(ctx, board_id, mask, &i, &e)) {
        board_component_t *board = &boards[i];

        if (board->game_state != STATE_PLAYING) {
            const vec4_t color = (vec4_t) { 1, 1, 1, 1 };
            const char *reason = (board->game_state == STATE_DRAW_BY_STALEMATE) ? "STALEMATE" : "CHECKMATE";
            const char *winner = (board->game_state == STATE_DRAW_BY_STALEMATE) ? "Draw." : 
                (board->game_state == STATE_WHITE_WIN_BY_CHECKMATE ? "White wins." : "Black wins."); 

            const rect_t window_r = window_api->rect();
            rect_t r = rect_inset((rect_t) { 0, window_r.h * 0.5f, window_r.w, 0 }, 0, -90.f);
            rect_t top = rect_divide_y(r, 0.f, 2, 0);
            rect_t bot = rect_divide_y(r, 0.f, 2, 1);
            
            im2d->text_utf8(top, reason, color, TEXT_ALIGN_CENTER, font_default, 1.f);
            im2d->text_utf8(bot, winner, color, TEXT_ALIGN_CENTER, font_default, 1.f);

            break;
        }

        ++i;
    }
}