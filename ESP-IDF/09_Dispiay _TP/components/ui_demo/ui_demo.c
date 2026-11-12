#include "ui_demo.h"

#include <string.h>
#include "lvgl.h"
#include "demos/widgets/lv_demo_widgets.h"
#include "esp_log.h"

static const char *TAG = "UI_DEMO";

static lv_obj_t *find_label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) &&
        strcmp(lv_label_get_text(root), text) == 0) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        lv_obj_t *label = find_label(lv_obj_get_child(root, i), text);
        if (label != NULL) {
            return label;
        }
    }
    return NULL;
}

/* Resolve the demo's controls by label instead of relying on child indices. */
static lv_obj_t *find_button(lv_obj_t *root, const char *text)
{
    lv_obj_t *label = find_label(root, text);
    lv_obj_t *parent = label != NULL ? lv_obj_get_parent(label) : NULL;
    return parent != NULL && lv_obj_check_type(parent, &lv_btn_class) ? parent : NULL;
}

static esp_err_t adapt_profile_buttons(void)
{
    /* The stock medium layout starts at 321px but fixes both buttons at 120px. */
    const lv_coord_t width = lv_disp_get_hor_res(NULL);
    if (width <= 320 || width > 480) {
        return ESP_OK;
    }
    lv_obj_t *logout = find_button(lv_scr_act(), "Log out");
    lv_obj_t *invite = find_button(lv_scr_act(), "Invite");
    if (logout == NULL || invite == NULL ||
        lv_obj_get_parent(logout) != lv_obj_get_parent(invite)) {
        ESP_LOGE(TAG, "Widgets demo profile buttons not found");
        return ESP_ERR_NOT_FOUND;
    }

    lv_obj_t *panel = lv_obj_get_parent(logout);
    const lv_coord_t *columns = lv_obj_get_style_grid_column_dsc_array(panel, 0);
    uint8_t column_count = 0;
    while (columns != NULL && column_count < 16 &&
           columns[column_count] != LV_GRID_TEMPLATE_LAST) {
        ++column_count;
    }
    if (column_count == 0 || column_count == 16) {
        return ESP_ERR_INVALID_STATE;
    }
    const lv_coord_t row = lv_obj_get_style_grid_cell_row_pos(logout, 0);
    lv_obj_t *buttons = lv_obj_create(panel);
    lv_obj_remove_style_all(buttons);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(buttons, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(buttons, LV_GRID_ALIGN_STRETCH, 0, column_count,
                         LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 12, 0);

    lv_obj_t *controls[] = {logout, invite};
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        lv_obj_set_parent(controls[i], buttons);
        lv_obj_set_width(controls[i], 0);
        lv_obj_set_style_min_width(controls[i], 0, 0);
        lv_obj_set_flex_grow(controls[i], 1);
    }
    lv_obj_update_layout(panel);

    lv_area_t left, right, row_area;
    lv_obj_get_coords(logout, &left);
    lv_obj_get_coords(invite, &right);
    lv_obj_get_coords(buttons, &row_area);
    if (left.x2 >= right.x1 || left.x1 < row_area.x1 ||
        right.x2 > row_area.x2) {
        ESP_LOGE(TAG, "Profile button layout exceeds its row");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Profile buttons: widths=%d/%d, gap=%dpx",
             (int)lv_obj_get_width(logout), (int)lv_obj_get_width(invite),
             (int)(right.x1 - left.x2 - 1));
    return ESP_OK;
}

static void sync_shop_section_width(lv_event_t *event)
{
    lv_obj_t *products = lv_event_get_target(event);
    lv_obj_t *notifications = lv_event_get_user_data(event);
    lv_obj_set_width(notifications, lv_obj_get_width(products));
}

static esp_err_t adapt_shop_products(void)
{
    const lv_coord_t width = lv_disp_get_hor_res(NULL);
    if (width <= 320 || width > 480) {
        return ESP_OK;
    }
    lv_obj_t *title = find_label(lv_scr_act(), "Top products");
    lv_obj_t *notification_title = find_label(lv_scr_act(), "Notification");
    if (title == NULL || notification_title == NULL) {
        ESP_LOGE(TAG, "Widgets demo shop sections not found");
        return ESP_ERR_NOT_FOUND;
    }

    lv_obj_t *products = lv_obj_get_parent(title);
    lv_obj_t *notifications = lv_obj_get_parent(notification_title);
    if (lv_obj_get_parent(products) != lv_obj_get_parent(notifications)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Each section gets a full row on the narrow display. */
    lv_obj_t *sections[] = {products, notifications};
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        lv_obj_set_width(sections[i], LV_PCT(100));
        lv_obj_set_flex_grow(sections[i], 0);
        lv_obj_add_flag(sections[i], LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_width(notification_title, LV_PCT(100));
    /* Match the rendered panel width, including after parent size changes. */
    lv_obj_add_event_cb(products, sync_shop_section_width,
                        LV_EVENT_SIZE_CHANGED, notifications);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(notifications); ++i) {
        lv_obj_t *child = lv_obj_get_child(notifications, i);
        if (lv_obj_check_type(child, &lv_checkbox_class)) {
            lv_obj_set_width(child, LV_PCT(100));
        }
    }

    /* Reserve the price's natural width; only the name/category column wraps. */
    static const lv_coord_t columns[] = {
        LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {
        LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST
    };
    uint32_t item_count = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(products); ++i) {
        lv_obj_t *item = lv_obj_get_child(products, i);
        if (item == title) {
            continue;
        }
        /* LVGL 8.4's create_shop_item creates image, name, category, price. */
        if (lv_obj_get_child_cnt(item) != 4) {
            return ESP_ERR_INVALID_STATE;
        }
        lv_obj_t *image = lv_obj_get_child(item, 0);
        lv_obj_t *name = lv_obj_get_child(item, 1);
        lv_obj_t *category = lv_obj_get_child(item, 2);
        lv_obj_t *price = lv_obj_get_child(item, 3);
        if (!lv_obj_check_type(image, &lv_img_class) ||
            !lv_obj_check_type(name, &lv_label_class) ||
            !lv_obj_check_type(category, &lv_label_class) ||
            !lv_obj_check_type(price, &lv_label_class)) {
            return ESP_ERR_INVALID_STATE;
        }
        lv_obj_set_grid_dsc_array(item, columns, rows);
        lv_obj_set_style_pad_column(item, 8, 0);
        /* Single-row grid includes the image in the item's content height. */
        lv_obj_set_grid_cell(image, LV_GRID_ALIGN_START, 0, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_t *details = lv_obj_create(item);
        lv_obj_remove_style_all(details);
        lv_obj_clear_flag(details, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_height(details, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(details, 4, 0);
        lv_obj_set_grid_cell(details, LV_GRID_ALIGN_STRETCH, 1, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_parent(name, details);
        lv_obj_set_parent(category, details);
        lv_obj_set_width(name, LV_PCT(100));
        lv_obj_set_width(category, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(category, LV_LABEL_LONG_WRAP);
        lv_obj_set_grid_cell(price, LV_GRID_ALIGN_END, 2, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        ++item_count;
    }
    lv_obj_update_layout(lv_obj_get_parent(products));
    lv_obj_set_width(notifications, lv_obj_get_width(products));
    lv_obj_update_layout(lv_obj_get_parent(products));
    ESP_LOGI(TAG, "Shop section widths: Top products=%d, Notification=%d",
             (int)lv_obj_get_width(products), (int)lv_obj_get_width(notifications));
    ESP_LOGI(TAG, "Shop layout: %u products, full-width sections, separate price column",
             (unsigned)item_count);
    return ESP_OK;
}

esp_err_t ui_demo_create(void)
{
    lv_demo_widgets();
    esp_err_t err = adapt_profile_buttons();
    return err == ESP_OK ? adapt_shop_products() : err;
}
