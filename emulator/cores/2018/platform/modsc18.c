#include "sc18_sim.h"

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/smallint.h"
#include "extmod/modtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * 1. machine module (Pin, I2C)
 * ============================================================================ */

#define SC18_PIN_IN         (1)
#define SC18_PIN_OUT        (2)
#define SC18_PIN_PULL_UP    (1)
#define SC18_PIN_PULL_DOWN  (2)

typedef struct _sc18_pin_obj_t {
    mp_obj_base_t base;
    int pin_id;
} sc18_pin_obj_t;

static const mp_obj_type_t sc18_pin_type;

static void sc18_pin_init_helper(sc18_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,  MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_pull,  MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int mode = (int)args[ARG_mode].u_int;
    int pull = 0;
    if (args[ARG_pull].u_obj != mp_const_none) {
        pull = (int)mp_obj_get_int(args[ARG_pull].u_obj);
    }
    if (mode >= 0 || pull > 0) {
        sc18_gpio_init(self->pin_id, mode < 0 ? SC18_PIN_IN : mode, pull);
    }
    if (args[ARG_value].u_obj != mp_const_none) {
        sc18_gpio_write(self->pin_id, mp_obj_is_true(args[ARG_value].u_obj) ? 1 : 0);
    }
}

static mp_obj_t sc18_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    sc18_pin_obj_t *self = mp_obj_malloc(sc18_pin_obj_t, type);
    self->pin_id = (int)mp_obj_get_int(args[0]);

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    sc18_pin_init_helper(self, n_args - 1, args + 1, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sc18_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    sc18_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_args == 0) {
        return MP_OBJ_NEW_SMALL_INT(sc18_gpio_read(self->pin_id));
    } else {
        sc18_gpio_write(self->pin_id, mp_obj_is_true(args[0]) ? 1 : 0);
        return mp_const_none;
    }
}

static mp_obj_t sc18_pin_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    sc18_pin_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    sc18_pin_init_helper(self, n_args - 1, pos_args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(sc18_pin_init_obj, 1, sc18_pin_init);

static mp_obj_t sc18_pin_value(size_t n_args, const mp_obj_t *args) {
    return sc18_pin_call(args[0], n_args - 1, 0, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_pin_value_obj, 1, 2, sc18_pin_value);

static mp_obj_t sc18_pin_on(mp_obj_t self_in) {
    sc18_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    sc18_gpio_write(self->pin_id, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_pin_on_obj, sc18_pin_on);

static mp_obj_t sc18_pin_off(mp_obj_t self_in) {
    sc18_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    sc18_gpio_write(self->pin_id, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_pin_off_obj, sc18_pin_off);

static mp_obj_t sc18_pin_id(mp_obj_t self_in) {
    sc18_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->pin_id);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_pin_id_obj, sc18_pin_id);

static const mp_rom_map_elem_t sc18_pin_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init),      MP_ROM_PTR(&sc18_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),     MP_ROM_PTR(&sc18_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_on),        MP_ROM_PTR(&sc18_pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off),       MP_ROM_PTR(&sc18_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_id),        MP_ROM_PTR(&sc18_pin_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_IN),        MP_ROM_INT(SC18_PIN_IN) },
    { MP_ROM_QSTR(MP_QSTR_OUT),       MP_ROM_INT(SC18_PIN_OUT) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UP),   MP_ROM_INT(SC18_PIN_PULL_UP) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(SC18_PIN_PULL_DOWN) },
};
static MP_DEFINE_CONST_DICT(sc18_pin_locals_dict, sc18_pin_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    make_new, sc18_pin_make_new,
    call, sc18_pin_call,
    locals_dict, &sc18_pin_locals_dict
);

static int extract_pin_num(mp_obj_t obj) {
    if (obj == mp_const_none) {
        return -1;
    }
    if (mp_obj_is_type(obj, &sc18_pin_type)) {
        sc18_pin_obj_t *p = MP_OBJ_TO_PTR(obj);
        return p->pin_id;
    }
    if (mp_obj_is_int(obj)) {
        return (int)mp_obj_get_int(obj);
    }
    return -1;
}

typedef struct _sc18_i2c_obj_t {
    mp_obj_base_t base;
    int bus_id; /* 1 = IS31FL3730 matrix (SCL=26, SDA=25), 2 = Minibadges (SCL=22, SDA=21) */
    int scl_pin;
    int sda_pin;
    uint32_t freq;
} sc18_i2c_obj_t;

static const mp_obj_type_t sc18_i2c_type;

static mp_obj_t sc18_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_scl, ARG_sda, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,   MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_scl,  MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_sda,  MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = 100000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int scl = extract_pin_num(args[ARG_scl].u_obj);
    int sda = extract_pin_num(args[ARG_sda].u_obj);
    int id = (int)args[ARG_id].u_int;

    int bus_id = 1;
    if (scl == 22 || sda == 21 || id == 1) {
        bus_id = 2;
    } else if (scl == 26 || sda == 25 || id == 0) {
        bus_id = 1;
    }

    sc18_i2c_obj_t *self = mp_obj_malloc(sc18_i2c_obj_t, type);
    self->bus_id = bus_id;
    self->scl_pin = scl;
    self->sda_pin = sda;
    self->freq = (uint32_t)args[ARG_freq].u_int;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sc18_i2c_writeto(size_t n_args, const mp_obj_t *args) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t addr = (uint8_t)(mp_obj_get_int(args[1]) & 0x7F);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);
    bool stop = (n_args < 4) ? true : mp_obj_is_true(args[3]);

    if (self->bus_id == 1) {
        int rc = sc18_i2c1_writeto(addr, (const uint8_t *)bufinfo.buf, bufinfo.len);
        if (rc < 0) {
            mp_raise_OSError(MP_ENODEV);
        }
        return MP_OBJ_NEW_SMALL_INT(rc);
    } else {
        uint8_t hdr = (uint8_t)(addr << 1);
        sc18_i2c2_start();
        int ack = sc18_i2c2_write(&hdr, 1);
        if (ack == 0) {
            if (stop) {
                sc18_i2c2_stop();
            }
            mp_raise_OSError(MP_ENODEV);
        }
        int written = sc18_i2c2_write((const uint8_t *)bufinfo.buf, bufinfo.len);
        if (stop) {
            sc18_i2c2_stop();
        }
        return MP_OBJ_NEW_SMALL_INT(written);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_i2c_writeto_obj, 3, 4, sc18_i2c_writeto);

static mp_obj_t sc18_i2c_start(mp_obj_t self_in) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->bus_id == 2) {
        sc18_i2c2_start();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_i2c_start_obj, sc18_i2c_start);

static mp_obj_t sc18_i2c_stop(mp_obj_t self_in) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->bus_id == 2) {
        sc18_i2c2_stop();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_i2c_stop_obj, sc18_i2c_stop);

static mp_obj_t sc18_i2c_write(mp_obj_t self_in, mp_obj_t buf_in) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (self->bus_id == 2) {
        int acks = sc18_i2c2_write((const uint8_t *)bufinfo.buf, bufinfo.len);
        return MP_OBJ_NEW_SMALL_INT(acks);
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_i2c_write_obj, sc18_i2c_write);

static mp_obj_t sc18_i2c_readinto(size_t n_args, const mp_obj_t *args) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    bool nack = (n_args < 3) ? true : mp_obj_is_true(args[2]);
    if (self->bus_id == 2) {
        sc18_i2c2_readinto((uint8_t *)bufinfo.buf, bufinfo.len, nack);
    } else if (bufinfo.len > 0) {
        memset(bufinfo.buf, 0xFF, bufinfo.len);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_i2c_readinto_obj, 2, 3, sc18_i2c_readinto);

static mp_obj_t sc18_i2c_scan_method(mp_obj_t self_in) {
    sc18_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t addrs[16];
    int count = sc18_i2c_scan(self->bus_id, addrs, 16);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < count; i++) {
        mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(addrs[i]));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_i2c_scan_obj, sc18_i2c_scan_method);

static const mp_rom_map_elem_t sc18_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_writeto),  MP_ROM_PTR(&sc18_i2c_writeto_obj) },
    { MP_ROM_QSTR(MP_QSTR_start),    MP_ROM_PTR(&sc18_i2c_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),     MP_ROM_PTR(&sc18_i2c_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),    MP_ROM_PTR(&sc18_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&sc18_i2c_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan),     MP_ROM_PTR(&sc18_i2c_scan_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_i2c_locals_dict, sc18_i2c_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, sc18_i2c_make_new,
    locals_dict, &sc18_i2c_locals_dict
);

static mp_obj_t sc18_machine_freq(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return MP_OBJ_NEW_SMALL_INT(240000000);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_machine_freq_obj, 0, 1, sc18_machine_freq);

static const mp_rom_map_elem_t sc18_machine_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_machine) },
    { MP_ROM_QSTR(MP_QSTR_Pin),      MP_ROM_PTR(&sc18_pin_type) },
    { MP_ROM_QSTR(MP_QSTR_I2C),      MP_ROM_PTR(&sc18_i2c_type) },
    { MP_ROM_QSTR(MP_QSTR_freq),     MP_ROM_PTR(&sc18_machine_freq_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_machine_globals, sc18_machine_globals_table);

const mp_obj_module_t sc18_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_machine_globals,
};

MP_REGISTER_MODULE(MP_QSTR_machine, sc18_module_machine);

/* ============================================================================
 * 2. network module (WLAN STA_IF & AP_IF)
 * ============================================================================ */

#define SC18_NET_STA_IF             (0)
#define SC18_NET_AP_IF              (1)
#define SC18_NET_AUTH_OPEN          (0)
#define SC18_NET_AUTH_WEP           (1)
#define SC18_NET_AUTH_WPA_PSK       (2)
#define SC18_NET_AUTH_WPA2_PSK      (3)
#define SC18_NET_AUTH_WPA_WPA2_PSK  (4)

typedef struct _sc18_wlan_obj_t {
    mp_obj_base_t base;
    int iface;
} sc18_wlan_obj_t;

static const mp_obj_type_t sc18_wlan_type;

static mp_obj_t sc18_wlan_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    sc18_wlan_obj_t *self = mp_obj_malloc(sc18_wlan_obj_t, type);
    self->iface = (n_args >= 1) ? (int)mp_obj_get_int(args[0]) : SC18_NET_STA_IF;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sc18_wlan_active(size_t n_args, const mp_obj_t *args) {
    sc18_wlan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args == 1) {
        return mp_obj_new_bool(sc18_wlan_get_active(self->iface));
    } else {
        sc18_wlan_set_active(self->iface, mp_obj_is_true(args[1]));
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_wlan_active_obj, 1, 2, sc18_wlan_active);

static mp_obj_t sc18_wlan_connect(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    (void)kw_args;
    const char *ssid = (n_args >= 2 && pos_args[1] != mp_const_none) ? mp_obj_str_get_str(pos_args[1]) : "";
    const char *pass = (n_args >= 3 && pos_args[2] != mp_const_none) ? mp_obj_str_get_str(pos_args[2]) : "";
    sc18_wlan_sta_connect(ssid, pass);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(sc18_wlan_connect_obj, 1, sc18_wlan_connect);

static mp_obj_t sc18_wlan_disconnect(mp_obj_t self_in) {
    sc18_wlan_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->iface == SC18_NET_STA_IF) {
        sc18_wlan_set_active(SC18_NET_STA_IF, false);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_wlan_disconnect_obj, sc18_wlan_disconnect);

static mp_obj_t sc18_wlan_isconnected(mp_obj_t self_in) {
    sc18_wlan_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->iface == SC18_NET_AP_IF) {
        return mp_obj_new_bool(sc18_wlan_get_active(SC18_NET_AP_IF));
    }
    return mp_obj_new_bool(sc18_wlan_sta_isconnected());
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_wlan_isconnected_obj, sc18_wlan_isconnected);

static mp_obj_t sc18_wlan_ifconfig(size_t n_args, const mp_obj_t *args) {
    sc18_wlan_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (n_args > 1) {
        return mp_const_none;
    }
    const char *ip = (self->iface == SC18_NET_AP_IF) ? "192.168.4.1" : "10.20.18.42";
    const char *gw = (self->iface == SC18_NET_AP_IF) ? "192.168.4.1" : "10.20.18.1";
    mp_obj_t tuple[4] = {
        mp_obj_new_str_from_cstr(ip),
        mp_obj_new_str_from_cstr("255.255.255.0"),
        mp_obj_new_str_from_cstr(gw),
        mp_obj_new_str_from_cstr(gw),
    };
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_wlan_ifconfig_obj, 1, 2, sc18_wlan_ifconfig);

static mp_obj_t sc18_wlan_config(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    sc18_wlan_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    if (kw_args->used == 0) {
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("can query only one param"));
        }
        qstr param = mp_obj_str_get_qstr(pos_args[1]);
        if (param == MP_QSTR_mac) {
            uint8_t mac[6];
            sc18_wlan_get_mac(mac);
            return mp_obj_new_bytes(mac, 6);
        } else if (param == MP_QSTR_essid || param == MP_QSTR_ssid) {
            return mp_obj_new_str_from_cstr(self->iface == SC18_NET_AP_IF ? "SC_2018" : "MUSEUM");
        }
        mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
    }

    const char *essid = NULL;
    const char *password = NULL;
    int authmode = 4;

    for (size_t i = 0; i < kw_args->alloc; i++) {
        if (mp_map_slot_is_filled(kw_args, i)) {
            qstr k = mp_obj_str_get_qstr(kw_args->table[i].key);
            mp_obj_t v = kw_args->table[i].value;
            if (k == MP_QSTR_essid || k == MP_QSTR_ssid) {
                essid = mp_obj_str_get_str(v);
            } else if (k == MP_QSTR_password) {
                password = mp_obj_str_get_str(v);
            } else if (k == MP_QSTR_authmode) {
                authmode = (int)mp_obj_get_int(v);
            }
        }
    }
    if (self->iface == SC18_NET_AP_IF) {
        sc18_wlan_ap_config(essid, password, authmode);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(sc18_wlan_config_obj, 1, sc18_wlan_config);

static mp_obj_t sc18_wlan_scan(mp_obj_t self_in) {
    (void)self_in;
    static const uint8_t bssid[6] = { 0x22, 0x18, 0x5C, 0x00, 0x00, 0x01 };
    mp_obj_t item[6] = {
        mp_obj_new_bytes((const byte *)"MUSEUM", 6),
        mp_obj_new_bytes(bssid, 6),
        MP_OBJ_NEW_SMALL_INT(6),
        MP_OBJ_NEW_SMALL_INT(-42),
        MP_OBJ_NEW_SMALL_INT(SC18_NET_AUTH_WPA_WPA2_PSK),
        MP_OBJ_NEW_SMALL_INT(0),
    };
    mp_obj_t tup = mp_obj_new_tuple(6, item);
    return mp_obj_new_list(1, &tup);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_wlan_scan_obj, sc18_wlan_scan);

static const mp_rom_map_elem_t sc18_wlan_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_active),      MP_ROM_PTR(&sc18_wlan_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect),     MP_ROM_PTR(&sc18_wlan_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect),  MP_ROM_PTR(&sc18_wlan_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_isconnected), MP_ROM_PTR(&sc18_wlan_isconnected_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig),    MP_ROM_PTR(&sc18_wlan_ifconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR_config),      MP_ROM_PTR(&sc18_wlan_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan),        MP_ROM_PTR(&sc18_wlan_scan_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_wlan_locals_dict, sc18_wlan_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_wlan_type,
    MP_QSTR_WLAN,
    MP_TYPE_FLAG_NONE,
    make_new, sc18_wlan_make_new,
    locals_dict, &sc18_wlan_locals_dict
);

static const mp_rom_map_elem_t sc18_network_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),           MP_ROM_QSTR(MP_QSTR_network) },
    { MP_ROM_QSTR(MP_QSTR_WLAN),               MP_ROM_PTR(&sc18_wlan_type) },
    { MP_ROM_QSTR(MP_QSTR_STA_IF),             MP_ROM_INT(SC18_NET_STA_IF) },
    { MP_ROM_QSTR(MP_QSTR_AP_IF),              MP_ROM_INT(SC18_NET_AP_IF) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_OPEN),          MP_ROM_INT(SC18_NET_AUTH_OPEN) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WEP),           MP_ROM_INT(SC18_NET_AUTH_WEP) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA_PSK),       MP_ROM_INT(SC18_NET_AUTH_WPA_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA2_PSK),      MP_ROM_INT(SC18_NET_AUTH_WPA2_PSK) },
    { MP_ROM_QSTR(MP_QSTR_AUTH_WPA_WPA2_PSK),  MP_ROM_INT(SC18_NET_AUTH_WPA_WPA2_PSK) },
};
static MP_DEFINE_CONST_DICT(sc18_network_globals, sc18_network_globals_table);

const mp_obj_module_t sc18_module_network = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_network_globals,
};

MP_REGISTER_MODULE(MP_QSTR_network, sc18_module_network);

/* ============================================================================
 * 3. socket / usocket module (Virtual HTTP Server Bridge for microWebSrv)
 * ============================================================================ */

typedef struct _sc18_socket_obj_t {
    mp_obj_base_t base;
    int port;
    bool is_server;
    bool is_client;
    bool closed;
} sc18_socket_obj_t;

static const mp_obj_type_t sc18_socket_type;

static mp_obj_t sc18_socket_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)args;
    mp_arg_check_num(n_args, n_kw, 0, 3, false);
    sc18_socket_obj_t *self = mp_obj_malloc(sc18_socket_obj_t, type);
    self->port = 0;
    self->is_server = false;
    self->is_client = false;
    self->closed = false;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sc18_socket_setsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_setsockopt_obj, 4, 4, sc18_socket_setsockopt);

static mp_obj_t sc18_socket_settimeout(mp_obj_t self_in, mp_obj_t timeout_in) {
    (void)self_in;
    (void)timeout_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_socket_settimeout_obj, sc18_socket_settimeout);

static mp_obj_t sc18_socket_setblocking(mp_obj_t self_in, mp_obj_t flag_in) {
    (void)self_in;
    (void)flag_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_socket_setblocking_obj, sc18_socket_setblocking);

static mp_obj_t sc18_socket_bind(mp_obj_t self_in, mp_obj_t addr_in) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    size_t len = 0;
    mp_obj_t *items = NULL;
    mp_obj_get_array(addr_in, &len, &items);
    if (len >= 2) {
        self->port = (int)mp_obj_get_int(items[1]);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_socket_bind_obj, sc18_socket_bind);

static mp_obj_t sc18_socket_listen(size_t n_args, const mp_obj_t *args) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    (void)n_args;
    self->is_server = true;
    self->closed = false;
    sc18_http_srv_listen(self->port ? self->port : 80);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_listen_obj, 1, 2, sc18_socket_listen);

static mp_obj_t sc18_socket_accept(mp_obj_t self_in) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->closed || !self->is_server) {
        mp_raise_OSError(MP_EBADF);
    }
    int rc = sc18_http_srv_accept_wait();
    if (rc < 0 || self->closed) {
        mp_raise_OSError(MP_EBADF);
    }
    sc18_socket_obj_t *cli = mp_obj_malloc(sc18_socket_obj_t, &sc18_socket_type);
    cli->port = self->port;
    cli->is_server = false;
    cli->is_client = true;
    cli->closed = false;

    mp_obj_t addr_items[2] = {
        mp_obj_new_str_from_cstr("192.168.4.2"),
        MP_OBJ_NEW_SMALL_INT(52018),
    };
    mp_obj_t ret_items[2] = {
        MP_OBJ_FROM_PTR(cli),
        mp_obj_new_tuple(2, addr_items),
    };
    return mp_obj_new_tuple(2, ret_items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_socket_accept_obj, sc18_socket_accept);

static mp_obj_t sc18_socket_readline(mp_obj_t self_in) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->closed || !self->is_client) {
        return mp_const_empty_bytes;
    }
    uint8_t line[2048];
    int n = sc18_http_cli_readline(line, (int)sizeof(line));
    if (n <= 0) {
        return mp_const_empty_bytes;
    }
    return mp_obj_new_bytes(line, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_socket_readline_obj, sc18_socket_readline);

static mp_obj_t sc18_socket_read(size_t n_args, const mp_obj_t *args) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->closed || !self->is_client) {
        return mp_const_empty_bytes;
    }
    int want = 4096;
    if (n_args >= 2 && args[1] != mp_const_none) {
        want = (int)mp_obj_get_int(args[1]);
        if (want <= 0) {
            return mp_const_empty_bytes;
        }
        if (want > 16384) {
            want = 16384;
        }
    }
    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)want);
    int n = sc18_http_cli_read((uint8_t *)vstr.buf, want);
    if (n <= 0) {
        vstr_clear(&vstr);
        return mp_const_empty_bytes;
    }
    vstr.len = (size_t)n;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_read_obj, 1, 2, sc18_socket_read);

static mp_obj_t sc18_socket_write(size_t n_args, const mp_obj_t *args) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->closed || !self->is_client) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);
    size_t off = 0;
    size_t sz = bufinfo.len;
    if (n_args >= 3) {
        mp_int_t o = mp_obj_get_int(args[2]);
        if (o > 0) {
            off = (size_t)o > bufinfo.len ? bufinfo.len : (size_t)o;
        }
        sz = bufinfo.len - off;
    }
    if (n_args >= 4) {
        mp_int_t s = mp_obj_get_int(args[3]);
        if (s >= 0 && (size_t)s < sz) {
            sz = (size_t)s;
        }
    }
    int written = sc18_http_cli_write((const uint8_t *)bufinfo.buf + off, (int)sz);
    return MP_OBJ_NEW_SMALL_INT(written);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_write_obj, 2, 4, sc18_socket_write);

static mp_obj_t sc18_socket_makefile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_makefile_obj, 1, 3, sc18_socket_makefile);

static mp_obj_t sc18_socket_close(mp_obj_t self_in) {
    sc18_socket_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->closed) {
        self->closed = true;
        if (self->is_server) {
            sc18_http_srv_close();
        } else if (self->is_client) {
            sc18_http_cli_close();
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_socket_close_obj, sc18_socket_close);

static const mp_rom_map_elem_t sc18_socket_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_setsockopt),  MP_ROM_PTR(&sc18_socket_setsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_settimeout),  MP_ROM_PTR(&sc18_socket_settimeout_obj) },
    { MP_ROM_QSTR(MP_QSTR_setblocking), MP_ROM_PTR(&sc18_socket_setblocking_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind),        MP_ROM_PTR(&sc18_socket_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen),      MP_ROM_PTR(&sc18_socket_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept),      MP_ROM_PTR(&sc18_socket_accept_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline),    MP_ROM_PTR(&sc18_socket_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),        MP_ROM_PTR(&sc18_socket_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv),        MP_ROM_PTR(&sc18_socket_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),       MP_ROM_PTR(&sc18_socket_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_send),        MP_ROM_PTR(&sc18_socket_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendall),     MP_ROM_PTR(&sc18_socket_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_makefile),    MP_ROM_PTR(&sc18_socket_makefile_obj) },
    { MP_ROM_QSTR(MP_QSTR_close),       MP_ROM_PTR(&sc18_socket_close_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_socket_locals_dict, sc18_socket_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_socket_type,
    MP_QSTR_socket,
    MP_TYPE_FLAG_NONE,
    make_new, sc18_socket_make_new,
    locals_dict, &sc18_socket_locals_dict
);

static mp_obj_t sc18_socket_getaddrinfo(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_obj_t host = args[0];
    mp_obj_t port = args[1];
    mp_obj_t sockaddr[2] = { host, port };
    mp_obj_t entry[5] = {
        MP_OBJ_NEW_SMALL_INT(2), /* AF_INET */
        MP_OBJ_NEW_SMALL_INT(1), /* SOCK_STREAM */
        MP_OBJ_NEW_SMALL_INT(6), /* IPPROTO_TCP */
        MP_OBJ_NEW_QSTR(MP_QSTR_),
        mp_obj_new_tuple(2, sockaddr),
    };
    mp_obj_t tup = mp_obj_new_tuple(5, entry);
    return mp_obj_new_list(1, &tup);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_socket_getaddrinfo_obj, 2, 6, sc18_socket_getaddrinfo);

static const mp_rom_map_elem_t sc18_modsocket_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),     MP_ROM_QSTR(MP_QSTR_socket) },
    { MP_ROM_QSTR(MP_QSTR_socket),       MP_ROM_PTR(&sc18_socket_type) },
    { MP_ROM_QSTR(MP_QSTR_getaddrinfo),  MP_ROM_PTR(&sc18_socket_getaddrinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET),      MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_STREAM),  MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_DGRAM),   MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP),  MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_SOL_SOCKET),   MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SO_REUSEADDR), MP_ROM_INT(2) },
};
static MP_DEFINE_CONST_DICT(sc18_modsocket_globals, sc18_modsocket_globals_table);

const mp_obj_module_t sc18_module_socket = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_modsocket_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_socket, sc18_module_socket);

/* ============================================================================
 * 4. select / uselect module (Cooperative Poller for bundled uasyncio)
 * ============================================================================ */

typedef struct _sc18_poll_obj_t {
    mp_obj_base_t base;
} sc18_poll_obj_t;

static const mp_obj_type_t sc18_poll_type;

static mp_obj_t sc18_poll_register(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_poll_register_obj, 2, 3, sc18_poll_register);

static mp_obj_t sc18_poll_unregister(mp_obj_t self_in, mp_obj_t obj_in) {
    (void)self_in;
    (void)obj_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_poll_unregister_obj, sc18_poll_unregister);

static mp_obj_t sc18_poll_modify(mp_obj_t self_in, mp_obj_t obj_in, mp_obj_t eventmask_in) {
    (void)self_in;
    (void)obj_in;
    (void)eventmask_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(sc18_poll_modify_obj, sc18_poll_modify);

static void sc18_poll_wait_delay(size_t n_args, const mp_obj_t *args) {
    mp_int_t delay = -1;
    if (n_args >= 2 && args[1] != mp_const_none) {
        delay = mp_obj_get_int(args[1]);
    }
    if (delay > 0) {
        sc18_fiber_sleep_ms((uint32_t)delay);
    } else if (delay == 0) {
        sc18_fiber_yield_once();
    } else {
        sc18_fiber_sleep_ms(5);
    }
}

static mp_obj_t sc18_poll_poll(size_t n_args, const mp_obj_t *args) {
    sc18_poll_wait_delay(n_args, args);
    return mp_obj_new_list(0, NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_poll_poll_obj, 1, 3, sc18_poll_poll);

static mp_obj_t sc18_poll_ipoll(size_t n_args, const mp_obj_t *args) {
    sc18_poll_wait_delay(n_args, args);
    return mp_const_empty_tuple;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_poll_ipoll_obj, 1, 3, sc18_poll_ipoll);

static const mp_rom_map_elem_t sc18_poll_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_register),   MP_ROM_PTR(&sc18_poll_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_unregister), MP_ROM_PTR(&sc18_poll_unregister_obj) },
    { MP_ROM_QSTR(MP_QSTR_modify),     MP_ROM_PTR(&sc18_poll_modify_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll),       MP_ROM_PTR(&sc18_poll_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_ipoll),      MP_ROM_PTR(&sc18_poll_ipoll_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_poll_locals_dict, sc18_poll_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_poll_type,
    MP_QSTR_poll,
    MP_TYPE_FLAG_NONE,
    locals_dict, &sc18_poll_locals_dict
);

static mp_obj_t sc18_select_poll(void) {
    sc18_poll_obj_t *self = mp_obj_malloc(sc18_poll_obj_t, &sc18_poll_type);
    return MP_OBJ_FROM_PTR(self);
}
static MP_DEFINE_CONST_FUN_OBJ_0(sc18_select_poll_obj, sc18_select_poll);

static const mp_rom_map_elem_t sc18_modselect_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_select) },
    { MP_ROM_QSTR(MP_QSTR_poll),     MP_ROM_PTR(&sc18_select_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_POLLIN),   MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_POLLOUT),  MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_POLLERR),  MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_POLLHUP),  MP_ROM_INT(16) },
};
static MP_DEFINE_CONST_DICT(sc18_modselect_globals, sc18_modselect_globals_table);

const mp_obj_module_t sc18_module_select = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_modselect_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_select, sc18_module_select);

/* ============================================================================
 * 5. timeq / utimeq module (Min-Heap Priority Queue for bundled uasyncio v2)
 * ============================================================================ */

typedef struct {
    mp_uint_t ticks;
    uint32_t seq;
    mp_obj_t cb;
    mp_obj_t args;
} sc18_timeq_item_t;

typedef struct _sc18_timeq_obj_t {
    mp_obj_base_t base;
    size_t alloc;
    size_t len;
    uint32_t next_seq;
    sc18_timeq_item_t *items;
} sc18_timeq_obj_t;

static const mp_obj_type_t sc18_utimeq_type;

static inline mp_int_t sc18_ticks_diff(mp_uint_t end, mp_uint_t start) {
    return (mp_int_t)(((end - start + MICROPY_PY_TIME_TICKS_PERIOD / 2) & (MICROPY_PY_TIME_TICKS_PERIOD - 1))
        - MICROPY_PY_TIME_TICKS_PERIOD / 2);
}

static inline bool sc18_timeq_less(const sc18_timeq_item_t *a, const sc18_timeq_item_t *b) {
    mp_int_t diff = sc18_ticks_diff(a->ticks, b->ticks);
    if (diff != 0) {
        return diff < 0;
    }
    return (int32_t)(a->seq - b->seq) < 0;
}

static mp_obj_t sc18_utimeq_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_int_t cap = mp_obj_get_int(args[0]);
    if (cap < 4) {
        cap = 4;
    }
    sc18_timeq_obj_t *self = mp_obj_malloc(sc18_timeq_obj_t, type);
    self->alloc = (size_t)cap;
    self->len = 0;
    self->next_seq = 0;
    self->items = m_new(sc18_timeq_item_t, self->alloc);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sc18_utimeq_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    sc18_timeq_obj_t *self = MP_OBJ_TO_PTR(self_in);
    switch (op) {
        case MP_UNARY_OP_BOOL:
            return mp_obj_new_bool(self->len != 0);
        case MP_UNARY_OP_LEN:
            return MP_OBJ_NEW_SMALL_INT(self->len);
        default:
            return MP_OBJ_NULL;
    }
}

static mp_obj_t sc18_utimeq_push(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    sc18_timeq_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->len >= self->alloc) {
        size_t new_alloc = self->alloc * 2;
        self->items = m_renew(sc18_timeq_item_t, self->items, self->alloc, new_alloc);
        self->alloc = new_alloc;
    }

    sc18_timeq_item_t item;
    item.ticks = (mp_uint_t)mp_obj_get_int(args[1]) & (MICROPY_PY_TIME_TICKS_PERIOD - 1);
    item.seq = self->next_seq++;
    item.cb = args[2];
    item.args = args[3];

    size_t idx = self->len++;
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (!sc18_timeq_less(&item, &self->items[parent])) {
            break;
        }
        self->items[idx] = self->items[parent];
        idx = parent;
    }
    self->items[idx] = item;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sc18_utimeq_push_obj, 4, 4, sc18_utimeq_push);

static mp_obj_t sc18_utimeq_peektime(mp_obj_t self_in) {
    sc18_timeq_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->len == 0) {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("empty"));
    }
    return MP_OBJ_NEW_SMALL_INT(self->items[0].ticks);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_utimeq_peektime_obj, sc18_utimeq_peektime);

static mp_obj_t sc18_utimeq_pop(mp_obj_t self_in, mp_obj_t ret_list) {
    sc18_timeq_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->len == 0) {
        mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("empty"));
    }
    sc18_timeq_item_t top = self->items[0];
    mp_obj_subscr(ret_list, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(top.ticks));
    mp_obj_subscr(ret_list, MP_OBJ_NEW_SMALL_INT(1), top.cb);
    mp_obj_subscr(ret_list, MP_OBJ_NEW_SMALL_INT(2), top.args);

    self->len--;
    if (self->len > 0) {
        sc18_timeq_item_t last = self->items[self->len];
        size_t idx = 0;
        for (;;) {
            size_t left = idx * 2 + 1;
            size_t right = left + 1;
            if (left >= self->len) {
                break;
            }
            size_t smallest = left;
            if (right < self->len && sc18_timeq_less(&self->items[right], &self->items[left])) {
                smallest = right;
            }
            if (!sc18_timeq_less(&self->items[smallest], &last)) {
                break;
            }
            self->items[idx] = self->items[smallest];
            idx = smallest;
        }
        self->items[idx] = last;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(sc18_utimeq_pop_obj, sc18_utimeq_pop);

static const mp_rom_map_elem_t sc18_utimeq_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_push),     MP_ROM_PTR(&sc18_utimeq_push_obj) },
    { MP_ROM_QSTR(MP_QSTR_pop),      MP_ROM_PTR(&sc18_utimeq_pop_obj) },
    { MP_ROM_QSTR(MP_QSTR_peektime), MP_ROM_PTR(&sc18_utimeq_peektime_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_utimeq_locals_dict, sc18_utimeq_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_utimeq_type,
    MP_QSTR_utimeq,
    MP_TYPE_FLAG_NONE,
    make_new, sc18_utimeq_make_new,
    unary_op, sc18_utimeq_unary_op,
    locals_dict, &sc18_utimeq_locals_dict
);

static const mp_rom_map_elem_t sc18_modtimeq_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_utimeq) },
    { MP_ROM_QSTR(MP_QSTR_utimeq),   MP_ROM_PTR(&sc18_utimeq_type) },
};
static MP_DEFINE_CONST_DICT(sc18_modtimeq_globals, sc18_modtimeq_globals_table);

const mp_obj_module_t sc18_module_timeq = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_modtimeq_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_timeq, sc18_module_timeq);

/* ============================================================================
 * 6. requests / urequests module (In-Page Stand-in Hacker Challenge Server)
 * ============================================================================ */

typedef struct _sc18_response_obj_t {
    mp_obj_base_t base;
    int status_code;
    mp_obj_t text;
    mp_obj_t content;
} sc18_response_obj_t;

static const mp_obj_type_t sc18_response_type;

static mp_obj_t sc18_response_close(mp_obj_t self_in) {
    (void)self_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_response_close_obj, sc18_response_close);

static mp_obj_t sc18_response_json(mp_obj_t self_in) {
    sc18_response_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t ujson_mod = mp_import_name(MP_QSTR_ujson, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t loads_fn = mp_load_attr(ujson_mod, MP_QSTR_loads);
    return mp_call_function_1(loads_fn, self->text);
}
static MP_DEFINE_CONST_FUN_OBJ_1(sc18_response_json_obj, sc18_response_json);

static const mp_rom_map_elem_t sc18_response_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&sc18_response_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_json),  MP_ROM_PTR(&sc18_response_json_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_response_locals_dict, sc18_response_locals_dict_table);

static void sc18_response_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    sc18_response_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }
    if (attr == MP_QSTR_text) {
        dest[0] = self->text;
    } else if (attr == MP_QSTR_content) {
        dest[0] = self->content;
    } else if (attr == MP_QSTR_status_code) {
        dest[0] = MP_OBJ_NEW_SMALL_INT(self->status_code);
    } else {
        dest[1] = MP_OBJ_SENTINEL;
    }
}

static MP_DEFINE_CONST_OBJ_TYPE(
    sc18_response_type,
    MP_QSTR_Response,
    MP_TYPE_FLAG_NONE,
    attr, sc18_response_attr,
    locals_dict, &sc18_response_locals_dict
);

static mp_obj_t sc18_requests_get(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    if (n_args < 1) {
        mp_raise_TypeError(MP_ERROR_TEXT("missing url"));
    }
    const char *url = mp_obj_str_get_str(pos_args[0]);
    const char *user_agent = "";

    mp_map_elem_t *hdr_elem = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_headers), MP_MAP_LOOKUP);
    if (hdr_elem != NULL && mp_obj_is_type(hdr_elem->value, &mp_type_dict)) {
        mp_map_t *hmap = mp_obj_dict_get_map(hdr_elem->value);
        for (size_t i = 0; i < hmap->alloc; i++) {
            if (mp_map_slot_is_filled(hmap, i) && mp_obj_is_str(hmap->table[i].key)) {
                const char *k = mp_obj_str_get_str(hmap->table[i].key);
                if (strcmp(k, "User-Agent") == 0 || strcmp(k, "user-agent") == 0) {
                    if (mp_obj_is_str(hmap->table[i].value)) {
                        user_agent = mp_obj_str_get_str(hmap->table[i].value);
                    }
                }
            }
        }
    }

    char body[512];
    body[0] = '\0';
    int len = sc18_score_http_get(url, user_agent, body, (int)sizeof(body));
    if (len < 0) {
        len = 0;
    }
    size_t slen = strnlen(body, sizeof(body) - 1);

    sc18_response_obj_t *resp = mp_obj_malloc(sc18_response_obj_t, &sc18_response_type);
    resp->status_code = 200;
    resp->text = mp_obj_new_str(body, slen);
    resp->content = mp_obj_new_bytes((const byte *)body, slen);
    return MP_OBJ_FROM_PTR(resp);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(sc18_requests_get_obj, 1, sc18_requests_get);

static const mp_rom_map_elem_t sc18_modrequests_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_urequests) },
    { MP_ROM_QSTR(MP_QSTR_get),      MP_ROM_PTR(&sc18_requests_get_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_modrequests_globals, sc18_modrequests_globals_table);

const mp_obj_module_t sc18_module_requests = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_modrequests_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_requests, sc18_module_requests);

/* ============================================================================
 * 7. ssl / ussl stub module (referenced optionally by uasyncio/__init__.py)
 * ============================================================================ */

static mp_obj_t sc18_ssl_wrap_socket(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    (void)n_args;
    (void)kw_args;
    return pos_args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(sc18_ssl_wrap_socket_obj, 1, sc18_ssl_wrap_socket);

static const mp_rom_map_elem_t sc18_modssl_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),    MP_ROM_QSTR(MP_QSTR_ssl) },
    { MP_ROM_QSTR(MP_QSTR_wrap_socket), MP_ROM_PTR(&sc18_ssl_wrap_socket_obj) },
};
static MP_DEFINE_CONST_DICT(sc18_modssl_globals, sc18_modssl_globals_table);

const mp_obj_module_t sc18_module_ssl = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sc18_modssl_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_ssl, sc18_module_ssl);
