#ifndef DWA_CONFIG_H
#define DWA_CONFIG_H
#define DWA_CONFIG_PATH "/Library/Application Support/DWA121/network.conf"
/* Serialized byte layout, independent of compiler padding and endianness. */
struct dwa_config { unsigned char magic[8], channel, ssid_length, reserved[2], ssid[32], pmk[32]; };
int dwa_config_valid(const struct dwa_config *c);
int dwa_config_read(const char *path,struct dwa_config *c);
int dwa_config_write(const char *path,const struct dwa_config *c);
void dwa_config_clear(void *p,unsigned int n);
#endif
