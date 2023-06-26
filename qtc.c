#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "vla.h"
#include "test_assets/assets.h"
#include "qtc.h"

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)       \
    ((byte)&0x80 ? '1' : '0'),     \
        ((byte)&0x40 ? '1' : '0'), \
        ((byte)&0x20 ? '1' : '0'), \
        ((byte)&0x10 ? '1' : '0'), \
        ((byte)&0x08 ? '1' : '0'), \
        ((byte)&0x04 ? '1' : '0'), \
        ((byte)&0x02 ? '1' : '0'), \
        ((byte)&0x01 ? '1' : '0')

#define NIBBLE_TO_BINARY_PATTERN "%c%c%c%c"
#define NIBBLE_TO_BINARY(nibble)     \
    ((nibble)&0x08 ? '1' : '0'),     \
        ((nibble)&0x04 ? '1' : '0'), \
        ((nibble)&0x02 ? '1' : '0'), \
        ((nibble)&0x01 ? '1' : '0')

#define GET_NIBBLE(p, l) ((*(p) >> ((l)*4)) & 0x0F)
#define SET_NIBBLE(p, l, n)           \
    do                                \
    {                                 \
        *(p) &= ~(0xF << ((l)*4));    \
        *(p) |= ((n)&0xF) << ((l)*4); \
    } while (0)

#define ADVANCE_NIBBLE(p, l) \
    do                       \
    {                        \
        if (l)               \
        {                    \
            (p)++;           \
        }                    \
        l = !l;              \
    } while (0)

typedef enum
{
    QT_DIR_WEST = 0x0,
    QT_DIR_EAST = 0x1,
    QT_DIR_NORTH = 0x0,
    QT_DIR_SOUTH = 0x2,

    QT_DIR_cnt
} qt_dir_e;

typedef enum
{
    QT_QUAD_NW = QT_DIR_NORTH | QT_DIR_WEST,
    QT_QUAD_NE = QT_DIR_NORTH | QT_DIR_EAST,
    QT_QUAD_SW = QT_DIR_SOUTH | QT_DIR_WEST,
    QT_QUAD_SE = QT_DIR_SOUTH | QT_DIR_EAST,

    QT_QUAD_cnt
} qt_quad_e;

uint32_t interleave_u16_zeroes(uint16_t u16)
{
    uint32_t u32 = u16;

    u32 = (u32 | (u32 << 8)) & 0b00000000111111110000000011111111;
    u32 = (u32 | (u32 << 4)) & 0b00001111000011110000111100001111;
    u32 = (u32 | (u32 << 2)) & 0b00110011001100110011001100110011;
    u32 = (u32 | (u32 << 1)) & 0b01010101010101010101010101010101;

    return u32;
}

uint32_t xy_to_morton(uint16_t x, uint16_t y)
{
    return interleave_u16_zeroes(x) | (interleave_u16_zeroes(y) << 1);
}

uint16_t get_u32_even_bits(uint32_t u32)
{
    u32 = u32 & 0x55555555;
    u32 = (u32 | (u32 >> 1)) & 0x33333333;
    u32 = (u32 | (u32 >> 2)) & 0x0F0F0F0F;
    u32 = (u32 | (u32 >> 4)) & 0x00FF00FF;
    u32 = (u32 | (u32 >> 8)) & 0x0000FFFF;
    return (uint16_t)u32;
}

void morton_to_xy(uint32_t morton, uint16_t *x, uint16_t *y)
{
    *x = get_u32_even_bits(morton);
    *y = get_u32_even_bits(morton >> 1);
}

void morton_inc_x(uint32_t *morton)
{
    uint32_t xsum = (*morton | 0xAAAAAAAA) + 1;
    *morton = (xsum & 0x55555555) | (*morton & 0xAAAAAAAA);
}

void morton_set_zero_x(uint32_t *morton)
{
    *morton &= ~(0x55555555);
}

void morton_inc_y(uint32_t *morton)
{
    uint32_t ysum = (*morton | 0x55555555) + 2;
    *morton = (ysum & 0xAAAAAAAA) | (*morton & 0x55555555);
}

void morton_set_zero_y(uint32_t *morton)
{
    *morton &= ~(0xAAAAAAAA);
}

typedef struct qtc_ir_node_t
{
    struct qtc_ir_node_t *quads[4];
} qtc_ir_node_t;

typedef struct
{
    qtc_ir_node_t *root;
    bool invert;
    uint8_t r;
    uint16_t w;
    uint16_t h;
} qtc_ir_t;

uint8_t qt_ir_node_to_nib(qtc_ir_node_t *node)
{
    uint8_t nib = 0;
    for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
    {
        nib |= (node->quads[quad] != NULL) << quad;
    }

    return nib;
}

void qt_ir_write_level(qtc_ir_node_t *node, bitstream_writer_t **bs, uint8_t lvl)
{
    if (node == NULL)
    {
        return;
    }

    if (lvl == 0)
    {
        uint8_t nib = qt_ir_node_to_nib(node);
        bitstream_write_bits(bs, nib, 4);
    }
    else
    {
        for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
        {
            qt_ir_write_level(node->quads[quad], bs, lvl - 1);
        }
    }
}

typedef struct
{
    uint8_t r;
    uint8_t *data;
    size_t data_size;
} lcqt_t, lqt_t;

lcqt_t *qt_ir_to_lcqt(qtc_ir_t *ir)
{
    bitstream_writer_t *bs = bitstream_writer_create();
    assert(bs != NULL);

    uint8_t r = ir->r;

    for (uint8_t lvl = 0; lvl < r; lvl++)
    {
        qt_ir_write_level(ir->root, &bs, lvl);
    }

    lcqt_t *lcqt = malloc(sizeof(lcqt_t));
    assert(lcqt != NULL);

    lcqt->r = r;
    bitstream_writer_get_data_copy(bs, &(lcqt->data), &(lcqt->data_size));

    free(bs);

    return lcqt;
}

void nibble_array_write(uint8_t *p, size_t i, uint8_t val)
{
    p[i / 2] &= ~(0xF << ((i & 1) * 4));
    p[i / 2] |= (val & 0xF) << ((i & 1) * 4);
}

uint8_t nibble_array_read(uint8_t *p, size_t i)
{
    return (p[i / 2] >> ((i & 1) * 4));
}

/**
 * Fill a subtree of a linear quadtree with 1s.
 *
 * @param qt pointer to quadtree array
 * @param qt_n node count of whole quadtree
 * @param node_i index of subtree's root node
 */
static void fill_ones(uint8_t *qt, size_t qt_n, size_t st_i)
{
    size_t nodes_to_fill = 1;
    while (st_i < qt_n)
    {
        for (size_t i = 0; i < nodes_to_fill; i++)
        {
            nibble_array_write(qt, st_i + i, 0xF);
        }
        st_i *= 4;
        nodes_to_fill *= 4;
    }
}

void qtc_to_qt(uint8_t const *const qtc, uint8_t r, uint8_t const **qt, size_t *qt_n)
{
    // calc the number of nodes in a perfect quad tree with height r
    // # nodes = (4^r - 1) / 3
    size_t qt_node_cnt = ((1 << (2 * r + 2)) - 1) / 3;

    // allocate space for the decompressed quad tree
    *qt = malloc((qt_node_cnt + 1) / 2);
    assert(*qt != NULL);

    memset(*qt, 0, qt_node_cnt);

    size_t qt_parent_i = 0;
    size_t qt_child_i = 0;
    size_t qtc_node_i = 0;

    // copy the first node of the compressed quad tree (the root)
    nibble_array_write(*qt, qt_child_i++, nibble_array_read(qtc, qtc_node_i++));

    while (qt_child_i < qt_node_cnt)
    {
        // read one node
        uint8_t parent = nibble_array_read(*qt, qt_parent_i);

        if (parent != 0 && nibble_array_read(qtc, qtc_node_i) == 0b0000)
        {
            qtc_node_i++;

            fill_ones(*qt, qt_node_cnt, qt_parent_i);
        }
        else
        {
            // write 4 nodes
            for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
            {
                if ((parent >> quad) & 0x1 && nibble_array_read(qt, qt_child_i) == 0)
                {
                    nibble_array_write(*qt, qt_child_i, nibble_array_read(qtc, qtc_node_i++));
                }
                qt_child_i++;
            }
        }
        qt_parent_i++;
    }
}

void qtc_decode(uint8_t *qtc, uint16_t w, uint16_t h, uint8_t **out, size_t *out_n)
{
    uint8_t r = 0;
    while ((1 << (r + 1)) < w)
    {
        r++;
    }

    while ((1 << (r + 1)) < h)
    {
        r++;
    }

    // calc the number of nodes in a perfect quad tree with height r
    // # nodes = (4^r - 1) / 3
    size_t qt_node_cnt = ((1 << (2 * r + 2)) - 1) / 3;

    // allocate space for the decompressed quad tree
    uint8_t *qt = malloc((qt_node_cnt + 1) / 2);
    memset(qt, 0, qt_node_cnt);

    size_t qt_rd_i = 0;
    size_t qt_wr_i = 0;
    size_t qtc_rd_i = 0;

    // copy the first node of the compressed quad tree (the root)
    nibble_array_write(qt, nibble_array_read(qtc, qtc_rd_i++), qt_wr_i++);

    while (qt_wr_i < qt_node_cnt)
    {
        // read one node
        uint8_t parent = nibble_array_read(qt, qt_rd_i);

        if (parent != 0 && nibble_array_read(qtc, qtc_rd_i) == 0b0000)
        {
            qtc_rd_i++;

            fill_ones(qt, qt_rd_i, qt_node_cnt);
        }
        else
        {
            // write 4 nodes
            for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
            {
                if ((parent >> quad) & 0x1 && nibble_array_read(qt, qt_wr_i) == 0)
                {
                    nibble_array_write(qt, qt_wr_i, nibble_array_read(qtc, qtc_rd_i++));
                }
                qt_wr_i++;
            }
        }
        qt_rd_i++;
    }

    uint16_t w_bytes = (w + 7) / 8;
    *out_n = h * w_bytes;

    *out = malloc(*out_n);
    memset(*out, 0, *out_n);
    uint8_t *row = *out;

    uint32_t morton = 0;

    for (uint16_t y = 0; y < h; y++, morton_inc_y(&morton))
    {
        morton_set_zero_x(&morton);
        for (uint16_t x = 0; x < w; x++, morton_inc_x(&morton))
        {
            qt_quad_e quad = morton & 3;
            bool set = (nibble_array_read(qt, qt_rd_i + morton / 4) >> quad) & 0x1;
            row[x / 8] |= set << (x & 7);
        }

        row += w_bytes;
    }

    free(qt);
}

void qt_ir_node_delete(qtc_ir_node_t *node)
{
    if (node == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < QT_QUAD_cnt; i++)
    {
        qt_ir_node_delete(node->quads[i]);
    }

    free(node);
}

bool qt_ir_node_get_val(qtc_ir_node_t *node, uint16_t x, uint16_t y, uint8_t r)
{
    if (r == 0)
    {
        return true;
    }

    bool all_null = true;
    for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
    {
        if (node->quads[quad] != NULL)
        {
            all_null = false;
            break;
        }
    }

    if (all_null)
    {
        return true;
    }

    r--;

    qt_quad_e quad = ((x >> r) & 0x1) | (((y >> r) & 0x1) << 1);

    if (node->quads[quad])
    {
        return qt_ir_node_get_val(node->quads[quad], x, y, r);
    }

    return false;
}

bool qt_ir_get_val(qtc_ir_t *ir, uint16_t x, uint16_t y)
{
    return qt_ir_node_get_val(ir->root, x, y, ir->r);
}

size_t qt_ir_nodes_count(qtc_ir_node_t *node)
{
    if (node == NULL)
    {
        return 0;
    }

    size_t sub_count = 0;
    for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
    {
        sub_count += qt_ir_nodes_count(node->quads[quad]);
    }

    return 1 + sub_count;
}

qtc_ir_t *qt_ir_from_bmp(const uint8_t *bmp_arr, uint16_t pix_w, uint16_t pix_h)
{
    uint8_t r = 0;
    while ((1 << r) < pix_w)
    {
        r++;
    }

    while ((1 << r) < pix_h)
    {
        r++;
    }

    uint16_t qt_w = (1 << r);

    uint16_t bmp_w_bytes = (pix_w + 7) / 8;
    const uint8_t *row = bmp_arr;

    uint32_t leaf_cnt = qt_w * qt_w;

    qtc_ir_node_t **lvl_nodes = malloc(sizeof(qtc_ir_node_t *) * leaf_cnt);

    for (uint16_t y = 0; y < qt_w; y++)
    {
        for (uint16_t x = 0; x < qt_w; x++)
        {
            qtc_ir_node_t *node = NULL;
            if (x < pix_w && y < pix_h)
            {
                if ((row[x / 8] >> (x % 8)) & 0x1)
                {
                    node = malloc(sizeof(qtc_ir_node_t));
                    for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
                    {
                        node->quads[quad] = NULL;
                    }
                }
            }

            uint32_t pix_mc = xy_to_morton(x, y);
            lvl_nodes[pix_mc] = node;
        }

        row += bmp_w_bytes;
    }

    uint32_t lvl_width = leaf_cnt;

    while (lvl_width > 1)
    {
        uint32_t prev_lvl_width = lvl_width;
        lvl_width /= 4;
        qtc_ir_node_t **prev_lvl_nodes = lvl_nodes;
        lvl_nodes = malloc(sizeof(qtc_ir_node_t *) * lvl_width);

        for (uint32_t i = 0; i < lvl_width; i++)
        {
            qtc_ir_node_t *node = NULL;

            bool child_not_null = false;
            for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
            {
                if (prev_lvl_nodes[quad] != NULL)
                {
                    child_not_null = true;
                    break;
                }
            }

            if (child_not_null)
            {
                node = malloc(sizeof(qtc_ir_node_t));
                for (uint8_t quad = 0; quad < QT_QUAD_cnt; quad++)
                {
                    node->quads[quad] = *prev_lvl_nodes;
                    prev_lvl_nodes++;
                }
            }
            else
            {
                prev_lvl_nodes += QT_QUAD_cnt;
            }

            lvl_nodes[i] = node;
        }

        free(prev_lvl_nodes - prev_lvl_width);
    }

    qtc_ir_t *qt_ir = malloc(sizeof(qt_ir[0]));

    qt_ir->root = *lvl_nodes;
    qt_ir->r = r;
    qt_ir->w = pix_w;
    qt_ir->h = pix_h;

    return qt_ir;
}

qtc_ir_t *qt_ir_create(uint8_t *pix_arr, uint8_t r)
{
    uint16_t w = 1 << r;

    size_t pix_cnt = w * w;

    qtc_ir_node_t **lvl_nodes = malloc(sizeof(qtc_ir_node_t *) * pix_cnt);

    uint8_t bit = 0;

    // populate leaves with pixel values
    for (uint16_t y = 0; y < w; y++)
    {
        for (uint16_t x = 0; x < w; x++)
        {
            qtc_ir_node_t *node = NULL;

            if ((*pix_arr >> bit) & 0x1)
            {
                node = malloc(sizeof(qtc_ir_node_t));
                for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
                {
                    node->quads[quad] = NULL;
                }
            }

            size_t pix_mc = xy_to_morton(x, y);
            lvl_nodes[pix_mc] = node;

            bit++;

            if (bit == 8)
            {
                pix_arr++;
                bit = 0;
            }
        }
    }

    size_t lvl_width = pix_cnt;

    while (lvl_width > 1)
    {
        size_t prev_lvl_width = lvl_width;
        lvl_width /= 4;
        qtc_ir_node_t **prev_lvl_nodes = lvl_nodes;
        lvl_nodes = malloc(sizeof(qtc_ir_node_t *) * lvl_width);

        for (size_t i = 0; i < lvl_width; i++)
        {
            qtc_ir_node_t *node = NULL;

            bool child_not_null = false;
            for (uint8_t quad = 0; quad < QT_QUAD_cnt; quad++)
            {
                child_not_null = child_not_null || (prev_lvl_nodes[quad] != NULL);
            }

            if (child_not_null)
            {
                node = malloc(sizeof(qtc_ir_node_t));
                for (uint8_t quad = 0; quad < QT_QUAD_cnt; quad++)
                {
                    node->quads[quad] = *prev_lvl_nodes;
                    prev_lvl_nodes++;
                }
            }
            else
            {
                prev_lvl_nodes += QT_QUAD_cnt;
            }

            lvl_nodes[i] = node;
        }

        free(prev_lvl_nodes - prev_lvl_width);
    }

    qtc_ir_t *qt_ir = malloc(sizeof(qt_ir[0]));

    qt_ir->root = *lvl_nodes;
    qt_ir->r = r;
    qt_ir->w = w;
    qt_ir->h = w;

    return qt_ir;
}

bool qt_ir_node_compress(qtc_ir_node_t *node, uint8_t r)
{
    if (node == NULL)
    {
        return false;
    }

    if (r == 0)
    {
        return true;
    }

    bool all_set = true;

    for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
    {
        if (!qt_ir_node_compress(node->quads[quad], r - 1))
        {
            all_set = false;
        }
    }

    if (all_set)
    {
        for (qt_quad_e quad = 0; quad < QT_QUAD_cnt; quad++)
        {
            qt_ir_node_delete(node->quads[quad]);
            node->quads[quad] = NULL;
        }
    }

    return all_set;
}

void qt_ir_compress(qtc_ir_t *ir)
{
    qt_ir_node_compress(ir->root, ir->r);
}

bool qt_ir_nodes_equal(qtc_ir_node_t *a, qtc_ir_node_t *b)
{
    if (a == NULL || b == NULL)
    {
        return a == b;
    }

    return qt_ir_nodes_equal(a->quads[0], b->quads[0]) &&
           qt_ir_nodes_equal(a->quads[1], b->quads[1]) &&
           qt_ir_nodes_equal(a->quads[2], b->quads[2]) &&
           qt_ir_nodes_equal(a->quads[3], b->quads[3]);
}

void qt_node_print_tree(qtc_ir_node_t *node, uint8_t tabs, qt_quad_e quad)
{
    if (node != NULL)
    {
        for (uint8_t i = 0; i < tabs; i++)
        {
            printf("\t");
        }

        printf("%u ", tabs);

        switch (quad)
        {
        case QT_QUAD_NW:
            printf("NW: ");
            break;
        case QT_QUAD_NE:
            printf("NE: ");
            break;
        case QT_QUAD_SE:
            printf("SE: ");
            break;
        case QT_QUAD_SW:
            printf("SW: ");
            break;
        default:
            break;
        }

        printf("\n");

        for (uint8_t i = 0; i < QT_QUAD_cnt; i++)
        {
            qt_node_print_tree(node->quads[i], tabs + 1, i);
        }
    }
}

void qt_ir_print_tree(qtc_ir_t *ir)
{
    if (ir->root)
    {
        for (uint8_t i = 0; i < QT_QUAD_cnt; i++)
        {
            qt_node_print_tree(ir->root->quads[i], 0, i);
        }
    }
}

void qt_ir_print_img(qtc_ir_t *ir)
{
    for (uint16_t y = 0; y < ir->h; y++)
    {
        for (uint16_t x = 0; x < ir->w; x++)
        {
            if (qt_ir_get_val(ir, x, y))
            {
                printf("⬜");
            }
            else
            {
                printf("⬛");
            }
        }
        printf("\n");
    }
}

void qt_ir_node_print_img(qtc_ir_node_t *node, uint8_t r)
{
    for (uint16_t y = 0; y < (1 << r); y++)
    {
        for (uint16_t x = 0; x < (1 << r); x++)
        {
            if (qt_ir_node_get_val(node, x, y, r))
            {
                printf("⬜");
            }
            else
            {
                printf("⬛");
            }
        }
        printf("\n");
    }
}

typedef struct
{
    size_t byte_cnt;
    uint8_t data[];
} qt_lin_t;

void qt_comp_traverse(uint8_t *d, bool left_nibble, size_t right_nodes_cnt, int32_t x, int32_t y, int32_t half_w)
{
    uint8_t children = (*d >> (left_nibble * 4)) & 0x0F;

    if (!children)
    {
        return;
    }
}

void qt_comp_decompress(qt_lin_t *qtc, uint8_t **res, size_t res_n)
{
    size_t node_i = 0;

    uint8_t *d = malloc(sizeof(uint8_t));

    size_t alloced = 1;

    uint8_t bit_pos = 0;
    uint8_t mask = 0x0F;

    uint8_t node = (qtc->data[node_i / 2] >> (4 * (node_i & 1))) & mask;

    node_i++;

    while (node)
    {
        uint8_t node = (qtc->data[node_i / 2] >> (4 * (node_i & 1))) & mask;
        while (node)
        {
            if (node & 1)
            {
            }
        }

        node = (qtc->data[node_i / 2] >> (4 * (node_i & 1))) & mask;
        node_i++;
    }
}

int main()
{
    qtc_ir_t *ir = qt_ir_from_bmp(globe_xxs_bits, GLOBE_XXS_WIDTH, GLOBE_XXS_HEIGHT);

    printf("r: %u\n", ir->r);
    printf("Nodes: %lu\n", qt_ir_nodes_count(ir->root));

    qt_ir_compress(ir);
    printf("Nodes: %lu\n", qt_ir_nodes_count(ir->root));
    printf("%f\n", (GLOBE_XXS_WIDTH * GLOBE_XXS_HEIGHT / 8) / ((double)qt_ir_nodes_count(ir->root) / 2));

    lcqt_t *lcqt = qt_ir_to_lcqt(ir);
    printf("cr: %f\n", (GLOBE_XXS_WIDTH * GLOBE_XXS_HEIGHT / 8) / ((double)lcqt->data_size));
}