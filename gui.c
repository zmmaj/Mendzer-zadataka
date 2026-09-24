/*
 * Menadžer Zadataka — GUI verzija
 * Korak 1: prozor + meni (Meni → Izadji) + prazna lista
 */

 #include <errno.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <str.h>
 #include <inttypes.h>

 #include "top_gui.h"
 
 #define NAME  "taskmgr"
 


 static void stop_btn_clicked(ui_pbutton_t *pbutton, void *arg)
 {
     guitop_t *g = (guitop_t *)arg;
     (void)pbutton;
 
     uint64_t sel_id = get_selected_task_id(g);
     if (sel_id == 0) {
         printf("Nema selektovanog zadatka.\n");
         return;
     }
 
     errno_t rc = task_kill((task_id_t)sel_id);
     if (rc != EOK) {
         printf("Greska pri zaustavljanju zadatka %llu: %d\n",
             (unsigned long long)sel_id, (int)rc);
     } else {
         printf("Zadatak %llu zaustavljen.\n",
             (unsigned long long)sel_id);
     }
 
     /* Ukloni zadatak iz liste odmah — ne čekaj refresh */
     /* (opciono, timer će ga ionako ukloniti pri sledećem refresh-u) */
 }

static ui_pbutton_cb_t stop_btn_cb = {
    .clicked = stop_btn_clicked
};

static errno_t create_stop_button(guitop_t *g, gfx_rect_t *arect)
{
    ui_resource_t *res = ui_window_get_res(g->window);
    gfx_rect_t rect;
    errno_t rc;

    rc = ui_pbutton_create(res, "STOP zadatak", &g->stop_btn);
    if (rc != EOK) return rc;

    ui_pbutton_set_cb(g->stop_btn, &stop_btn_cb, (void *)g);

    /* Postavi rect — desno u redu sa menijem */
    if (ui_is_textmode(g->ui)) {
        rect.p1.x = arect->p1.x - STOP_BTN_MARGIN;
        rect.p0.x = rect.p1.x - STOP_BTN_WIDTH;
        rect.p0.y = arect->p0.y;
        rect.p1.y = arect->p0.y + 1;
    } else {
        rect.p1.x = arect->p1.x - STOP_BTN_MARGIN;
        rect.p0.x = rect.p1.x - STOP_BTN_WIDTH;
        rect.p0.y = arect->p0.y + 4;
        rect.p1.y = rect.p0.y + STOP_BTN_HEIGHT;
    }

    ui_pbutton_set_rect(g->stop_btn, &rect);

    return EOK;
}

 static load_t get_load_quick(void)
{
    size_t count;
    load_t *load = stats_get_load(&count);
    load_t ret = 0;

    if (load != NULL && count > 0) {
        ret = load[0];
        free(load);
    }

    return ret;
}

static void plan_timer(guitop_t *g, usec_t work_time)
{
    load_t load = get_load_quick();

    /* Ako je posao trajao duže od intervala — odmah na MAX */
    if (work_time > g->interval_us)
        g->interval_us = MAX_INTERVAL_US;

    /* Ako je load visok — uspori */
    if (load > LOAD_HIGH)
        g->interval_us += INIT_INTERVAL_US;
    /* Ako je load nizak i posao brz — ubrzaj */
    else if (load < LOAD_LOW && work_time < g->interval_us / 4)
        g->interval_us -= INIT_INTERVAL_US / 2;

    /* Ograniči */
    if (g->interval_us < MIN_INTERVAL_US)
        g->interval_us = MIN_INTERVAL_US;
    if (g->interval_us > MAX_INTERVAL_US)
        g->interval_us = MAX_INTERVAL_US;

    fibril_timer_set(g->timer, g->interval_us, timer_callback, g);
}

void timer_callback(void *arg)
{
    guitop_t *g = (guitop_t *)arg;
    struct timespec t0, t1;
    usec_t work_time;
    data_t new_data;
    const char *err;
    uint64_t sel_id;

    getuptime(&t0);

    /* 1) Zapamti selektovani ID PRE destroy-a */
    sel_id = get_selected_task_id(g);

    /* 2) Pročitaj nove podatke */
    err = read_data(&new_data);
    if (err != NULL) {
        printf("Greska pri citanju: %s\n", err);
        free_data(&new_data);
        goto reschedule;
    }

    /* 3) Izračunaj procente */
    compute_percentages(&g->data, &new_data);

    /* 4) Oslobodi staro */
    if (g->ima_starih_podataka)
        free_data(&g->data);

    /* 5) Prebaci novo */
    g->data = new_data;
    g->ima_starih_podataka = true;

    /* 6) Napuni tabelu + sortiraj po CPU */
    fill_table(&g->data);
    sort_table(&g->data.table);

    /* 7) Osveži listu (destroy + recreate) */
    top_list_refresh(g);

    /* 8) Vrati selekciju po ID-u */
    restore_selection(g, sel_id);

reschedule:
    getuptime(&t1);
    work_time = NSEC2USEC(ts_sub_diff(&t1, &t0));
    plan_timer(g, work_time);
}


 uint64_t get_selected_task_id(guitop_t *g)
{
    ui_list_entry_t *e = ui_list_get_cursor(g->list);
    if (e == NULL)
        return 0;

    void *arg = ui_list_entry_get_arg(e);

   // printf("SEL: arg=%p id=%llu\n", arg,
    //    (unsigned long long)(intptr_t)arg);

    return (uint64_t)(intptr_t) arg;
}

 void restore_selection(guitop_t *g, uint64_t task_id)
{
    if (task_id == 0){
   // printf("RESTORE: task_id=0, preskacem\n");
        return;
    }

   // printf("RESTORE: trazim task_id=%llu\n",
    //    (unsigned long long)task_id);


    ui_list_entry_t *e = ui_list_first(g->list);
    while (e != NULL) {
        void *arg = ui_list_entry_get_arg(e);
     //   printf("  RED: arg=%p id=%llu\n", arg,
      //      (unsigned long long)(intptr_t)arg);

        if ((uint64_t)(intptr_t) arg == task_id) {
          //  printf("  NADJEN!\n");
            ui_list_set_cursor(g->list, e);
            ui_list_cursor_center(g->list, e);   /* skroluj da bude vidljiv */
            return;
        }
        e = ui_list_next(e);
    }
    /* Ako zadatak više ne postoji — ostavi kursor gde jeste */
  //  printf("  NIJE NADJEN\n");
}

void top_list_refresh(guitop_t *g)
{
    ui_list_entry_attr_t attr;
    char line[MAX_LINE];
    errno_t rc;

    size_t rows = g->data.table.num_fields / g->data.table.num_columns;

    ui_lock(g->ui);

    /* 1) Ukloni listu iz fixed layout-a */
    ui_fixed_remove(g->fixed, ui_list_ctl(g->list));

    /* 2) Uništi staru listu */
    ui_list_destroy(g->list);
    g->list = NULL;

    /* 3) Napravi novu listu */
    rc = ui_list_create(g->window, true, &g->list);
    if (rc != EOK) {
        ui_unlock(g->ui);
        return;
    }

    /* 4) Postavi rect (isti kao pre) */
    ui_list_set_rect(g->list, &g->list_rect);

    /* 5) Dodaj u fixed */
    ui_fixed_add(g->fixed, ui_list_ctl(g->list));

    /* 6) Zaglavlje — arg = NULL */
    format_header(&g->data.table, line, sizeof(line));
    ui_list_entry_attr_init(&attr);
    attr.caption = line;
    attr.arg = NULL;
    ui_list_entry_append(g->list, &attr, NULL);

    /* 7) Redovi — arg = ID zadatka */
    for (size_t r = 0; r < rows; r++) {
        format_row(&g->data.table, r, line, sizeof(line));
        ui_list_entry_attr_init(&attr);
        attr.caption = line;

        field_t *f = g->data.table.fields +
            r * g->data.table.num_columns;
        attr.arg = (void *)(intptr_t) f[0].uint;

        ui_list_entry_append(g->list, &attr, NULL);
    }

    /* 8) Jedan render na kraju */
    (void) ui_window_paint(g->window);

    ui_unlock(g->ui);
}

 
 
 
 static void format_percent(fixed_float f, unsigned precision,
     char *buf, size_t bufsz)
 {
     /* upper/lower su uint64_t, f.lower != 0 */
     size_t off = 0;
     off += snprintf(buf + off, bufsz - off, "%3" PRIu64 ".",
         f.upper / f.lower);
 
     uint64_t rest = (f.upper % f.lower) * 10;
     for (unsigned i = 0; i < precision && off < bufsz - 1; i++) {
         off += snprintf(buf + off, bufsz - off, "%" PRIu64,
             rest / f.lower);
         rest = (rest % f.lower) * 10;
     }
 
     snprintf(buf + off, bufsz - off, "%%");
 }
 
 
 static void format_field(field_t *f, int width, char *buf, size_t bufsz)
 {
     uint64_t val;
     const char *psuffix;
     char suffix;
 
     switch (f->type) {
     case FIELD_EMPTY:
         snprintf(buf, bufsz, "%*s", width, "");
         break;
     case FIELD_UINT:
         snprintf(buf, bufsz, "%*" PRIu64, width, f->uint);
         break;
     case FIELD_UINT_SUFFIX_BIN:
         val = f->uint;
         width -= 3;
         bin_order_suffix(val, &val, &psuffix, true);
         snprintf(buf, bufsz, "%*" PRIu64 "%s", width, val, psuffix);
         break;
     case FIELD_UINT_SUFFIX_DEC:
         val = f->uint;
         width -= 1;
         order_suffix(val, &val, &suffix);
         snprintf(buf, bufsz, "%*" PRIu64 "%c", width, val, suffix);
         break;
     case FIELD_PERCENT:
         width -= 5;
         if (width > 2) width = 2;
         format_percent(f->fixed, width, buf, bufsz);
         break;
     case FIELD_STRING:
         snprintf(buf, bufsz, "%-*.*s", width, width, f->string);
         break;
     }
 }
 
 
void format_row(table_t *t, size_t row, char *buf, size_t bufsz)
 {
     size_t off = 0;
     field_t *f = t->fields + row * t->num_columns;
 
     for (size_t c = 0; c < t->num_columns && off < bufsz - 1; c++) {
         int width = t->columns[c].width;
         if (width == 0) width = 20;   /* fallback za "naziv" */
 
         if (c != 0) {
             buf[off++] = ' ';
         }
 
         char tmp[128];
         format_field(&f[c], width, tmp, sizeof(tmp));
 
         size_t len = str_length(tmp);
         if (off + len >= bufsz - 1)
             len = bufsz - 1 - off;
 
         memcpy(buf + off, tmp, len);
         off += len;
     }
     buf[off] = '\0';
 }
 
 
void format_header(table_t *t, char *buf, size_t bufsz)
 {
     size_t off = 0;
 
     for (size_t c = 0; c < t->num_columns && off < bufsz - 1; c++) {
         int width = t->columns[c].width;
         if (width == 0) width = 20;
 
         if (c != 0) {
             buf[off++] = ' ';
             buf[off++] = ' ';
         }
 
         const char *name = t->columns[c].name;
         size_t len = str_length(name);
         if (len > (size_t)width) len = width;
 
         if (c == 0) {
             /* Prva kolona — desno poravnaj (da bude iznad brojeva) */
             size_t pad = (size_t)width - len;
             while (pad > 0 && off < bufsz - 1) {
                 buf[off++] = ' ';
                 pad--;
             }
         }
 
         if (off + len >= bufsz - 1) len = bufsz - 1 - off;
         memcpy(buf + off, name, len);
         off += len;
 
         /* Dopuni do pune širine (samo za kolone koje nisu prva) */
         if (c != 0) {
             size_t pad = (size_t)width - len;
             while (pad > 0 && off < bufsz - 1) {
                 buf[off++] = ' ';
                 pad--;
             }
         }
     }
     buf[off] = '\0';
 }
 

 /* ---------- Prozor callback-ovi ---------- */
 
 static void wnd_close(ui_window_t *window, void *arg)
 {
     guitop_t *g = (guitop_t *)arg;
     (void)window;

     if (g->timer != NULL) {
        fibril_timer_clear(g->timer);
    }

     ui_quit(g->ui);
 }
 
 static void wnd_resize(ui_window_t *window, void *arg)
 {
     guitop_t *g = (guitop_t *)arg;
     gfx_rect_t arect;
     gfx_rect_t rect;
 
     ui_window_get_app_rect(window, &arect);
 
     /* Meni */
     rect.p0.x = arect.p0.x;
     rect.p0.y = arect.p0.y + MENU_BAR_Y_OFFSET;
     rect.p1.x = arect.p1.x;
     rect.p1.y = arect.p0.y + MENU_BAR_Y_OFFSET + MENU_BAR_HEIGHT;
     ui_menu_bar_set_rect(g->mbar, &rect);

  /* Dugme — desno u redu sa menijem */
  if (ui_is_textmode(g->ui)) {
    rect.p1.x = arect.p1.x - STOP_BTN_MARGIN;
    rect.p0.x = rect.p1.x - STOP_BTN_WIDTH;
    rect.p0.y = arect.p0.y;
    rect.p1.y = arect.p0.y + 1;
} else {
    rect.p1.x = arect.p1.x - STOP_BTN_MARGIN;
    rect.p0.x = rect.p1.x - STOP_BTN_WIDTH;
    rect.p0.y = arect.p0.y + 4;
    rect.p1.y = rect.p0.y + STOP_BTN_HEIGHT;
}
ui_pbutton_set_rect(g->stop_btn, &rect);

     /* Lista */
     rect.p0.x = arect.p0.x;
     rect.p0.y = arect.p0.y + LIST_Y_OFFSET;
     rect.p1 = arect.p1;
 
     g->list_rect = rect;               /* zapamti rect */
     ui_list_set_rect(g->list, &rect);
 
     (void) ui_window_paint(window);
 }
 
 static ui_window_cb_t window_cb = {
     .close  = wnd_close,
     .resize = wnd_resize,
 };
 
 /* ---------- Meni callback-ovi ---------- */
 
 static void menu_exit(ui_menu_entry_t *e, void *arg)
 {
     guitop_t *g = (guitop_t *)arg;
     (void)e;
     ui_quit(g->ui);
 }
 
 /* ---------- Kreiranje menija ---------- */
 
 static errno_t create_menus(guitop_t *g, gfx_rect_t *arect)
 {
     ui_menu_entry_t *e;
     gfx_rect_t rect;
     errno_t rc;
 
     rc = ui_menu_bar_create(g->ui, g->window, &g->mbar);
     if (rc != EOK) return rc;
 
     rc = ui_menu_dd_create(g->mbar, "~M~eni", NULL, &g->mmenu);
     if (rc != EOK) return rc;
 
     rc = ui_menu_entry_create(g->mmenu, "~I~zadji", "Alt-F4", &e);
     if (rc != EOK) return rc;
 
     ui_menu_entry_set_cb(e, menu_exit, g);
 
     if (ui_is_textmode(g->ui)) {
        rect.p0.y = arect->p0.y;
        rect.p1.y = arect->p0.y + 1;
    } else {
        rect.p0.y = arect->p0.y + 4;
        rect.p1.y = arect->p0.y + 26;
    }
    rect.p0.x = arect->p0.x;
    rect.p1.x = arect->p1.x;

     ui_menu_bar_set_rect(g->mbar, &rect);
 
     return EOK;
 }
 
 
 /* ---------- Kreiranje prazne liste sa zaglavljem ---------- */
 
 static errno_t create_list(guitop_t *g, gfx_rect_t *arect)
{
    ui_list_entry_attr_t attr;
    ui_list_entry_t *entry;
    errno_t rc;

    rc = ui_list_create(g->window, true, &g->list);
    if (rc != EOK) return rc;

    /* Prvo prazno zaglavlje — pravo zaglavlje dolazi u top_list_refresh */
    ui_list_entry_attr_init(&attr);
    attr.caption = "ID  %CPU  Mem  Ime";
    attr.arg = NULL;
    rc = ui_list_entry_append(g->list, &attr, &entry);
    if (rc != EOK) return rc;

    /* Postavi rect liste i zapamti ga */
    g->list_rect.p0.x = arect->p0.x;
    g->list_rect.p0.y = arect->p0.y + LIST_Y_OFFSET;
    g->list_rect.p1 = arect->p1;

    ui_list_set_rect(g->list, &g->list_rect);

    return EOK;
}
 
 
 /* ---------- main ---------- */
 
 int guitop_main(int argc, char *argv[])
 {
     const char *display_spec = UI_ANY_DEFAULT;
     guitop_t gui;
     ui_wnd_params_t params;
     ui_window_t *window;
     ui_t *ui;
     gfx_rect_t arect;          /* DODATO */
     errno_t rc;
     int i;
 
     /* Parsiranje -d <displej> */
     i = 1;
     while (i < argc) {
         if (str_cmp(argv[i], "-d") == 0) {
             ++i;
             if (i >= argc) {
                 printf("Argument nedostaje.\n");
                 return 1;
             }
             display_spec = argv[i++];
         } else {
             printf("Neispravna opcija '%s'.\n", argv[i]);
             return 1;
         }
     }
 
     rc = ui_create(display_spec, &ui);
     if (rc != EOK) {
         printf("Greska pri kreiranju UI na displeju %s.\n", display_spec);
         return 1;
     }
 
     gui.ui = ui;
 
     ui_wnd_params_init(&params);
     params.caption = "Menadžer Zadataka";
     params.rect.p0.x = 0;
     params.rect.p0.y = 0;
     params.rect.p1.x = 640;
     params.rect.p1.y = 400;
 
     rc = ui_window_create(ui, &params, &window);
     if (rc != EOK) {
         printf("Greska pri kreiranju prozora.\n");
         ui_destroy(ui);
         return 1;
     }
 
     gui.window = window;
     ui_window_set_cb(window, &window_cb, (void *)&gui);
 
     rc = ui_fixed_create(&gui.fixed);
     if (rc != EOK) {
         printf("Greska pri kreiranju layout-a.\n");
         goto error;
     }
 
     ui_window_get_app_rect(window, &arect);

     rc = create_menus(&gui, &arect);
     if (rc != EOK) {
         printf("Greska pri kreiranju menija.\n");
         goto error;
     }
 
     rc = create_list(&gui, &arect);
     if (rc != EOK) {
         printf("Greska pri kreiranju liste.\n");
         goto error;
     }

     rc = create_stop_button(&gui, &arect);
if (rc != EOK) {
    printf("Greska pri kreiranju dugmeta.\n");
    goto error;
}
 
     rc = ui_fixed_add(gui.fixed, ui_menu_bar_ctl(gui.mbar));
     if (rc != EOK)
         goto error;
 
     rc = ui_fixed_add(gui.fixed, ui_list_ctl(gui.list));
     if (rc != EOK) {
         goto error;
        }
   
        rc = ui_fixed_add(gui.fixed, ui_pbutton_ctl(gui.stop_btn));
       if (rc != EOK){
         goto error;
       }
 
        ui_window_add(window, ui_fixed_ctl(gui.fixed));

     rc = ui_window_paint(window);
     if (rc != EOK)
         goto error;
 

/* Prvo sinhrono čitanje — da lista ne bude prazna */
const char *err = read_data(&gui.data);
if (err != NULL) {
    printf("Greska: %s\n", err);
    goto error;
}

gui.ima_starih_podataka = true;
compute_percentages(&gui.data, &gui.data);
fill_table(&gui.data);
sort_table(&gui.data.table);
top_list_refresh(&gui);

/* Kreiraj timer */
gui.timer = fibril_timer_create(NULL);
if (gui.timer == NULL) {
    printf("Ne mogu da kreiram timer.\n");
    goto error;
}
gui.interval_us = INIT_INTERVAL_US;

/* Zakazi prvi poziv */
fibril_timer_set(gui.timer, gui.interval_us, timer_callback, &gui);




     ui_run(ui);
 
     ui_window_destroy(window);
     ui_destroy(ui);
     return 0;
 
 error:
     ui_window_destroy(window);
     ui_destroy(ui);
     return 1;
 }
