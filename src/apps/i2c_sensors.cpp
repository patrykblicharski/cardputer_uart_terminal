#include "i2c_sensors.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>

// ─── Wire helpers ─────────────────────────────────────────────────────────────

static bool wr(TwoWire& b, uint8_t a, uint8_t reg, uint8_t val) {
    b.beginTransmission(a);
    b.write(reg); b.write(val);
    return b.endTransmission() == 0;
}

static bool rd(TwoWire& b, uint8_t a, uint8_t reg, uint8_t* buf, uint8_t len) {
    b.beginTransmission(a);
    b.write(reg);
    if (b.endTransmission(false) != 0) return false;
    b.requestFrom(a, len);
    for (uint8_t i = 0; i < len; i++) {
        if (!b.available()) return false;
        buf[i] = b.read();
    }
    return true;
}

static uint8_t rd1(TwoWire& b, uint8_t a, uint8_t reg) {
    uint8_t v = 0; rd(b, a, reg, &v, 1); return v;
}

// ─── BMP280 / BME280 ─────────────────────────────────────────────────────────

struct BmpTrim {
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2,P3,P4,P5,P6,P7,P8,P9;
    uint8_t  H1; int16_t H2; uint8_t H3; int16_t H4,H5; int8_t H6;
    bool     is_bme;
};
static BmpTrim bmpTrim;
static int32_t bmp_tfine;

static const char* kBmpOsrs[] = {"Off","1x","2x","4x","8x","16x"};
static const char* kBmpFilt[] = {"Off","2","4","8","16"};
static const char* kBmpTsb[]  = {"0.5ms","62.5ms","125ms","250ms","500ms","1s","2s","4s"};

static void bmp_load_trim(uint8_t addr, TwoWire& bus) {
    uint8_t d[24];
    rd(bus, addr, 0x88, d, 24);
    bmpTrim.T1=(uint16_t)(d[1]<<8|d[0]); bmpTrim.T2=(int16_t)(d[3]<<8|d[2]); bmpTrim.T3=(int16_t)(d[5]<<8|d[4]);
    bmpTrim.P1=(uint16_t)(d[7]<<8|d[6]); bmpTrim.P2=(int16_t)(d[9]<<8|d[8]);
    bmpTrim.P3=(int16_t)(d[11]<<8|d[10]); bmpTrim.P4=(int16_t)(d[13]<<8|d[12]);
    bmpTrim.P5=(int16_t)(d[15]<<8|d[14]); bmpTrim.P6=(int16_t)(d[17]<<8|d[16]);
    bmpTrim.P7=(int16_t)(d[19]<<8|d[18]); bmpTrim.P8=(int16_t)(d[21]<<8|d[20]);
    bmpTrim.P9=(int16_t)(d[23]<<8|d[22]);
    bmpTrim.is_bme = (rd1(bus, addr, 0xD0) == 0x60);
    if (bmpTrim.is_bme) {
        bmpTrim.H1 = rd1(bus, addr, 0xA1);
        uint8_t h[7]; rd(bus, addr, 0xE1, h, 7);
        bmpTrim.H2=(int16_t)(h[1]<<8|h[0]); bmpTrim.H3=h[2];
        bmpTrim.H4=(int16_t)((int8_t)h[3]<<4|(h[4]&0x0F));
        bmpTrim.H5=(int16_t)((int8_t)h[5]<<4|(h[4]>>4));
        bmpTrim.H6=(int8_t)h[6];
    }
}

static float bmp_comp_temp(int32_t adc_T) {
    int32_t v1 = ((adc_T>>3)-((int32_t)bmpTrim.T1<<1))*(int32_t)bmpTrim.T2>>11;
    int32_t t  = (adc_T>>4)-(int32_t)bmpTrim.T1;
    int32_t v2 = ((t*t>>12)*(int32_t)bmpTrim.T3)>>14;
    bmp_tfine = v1+v2;
    return (float)((bmp_tfine*5+128)>>8)/100.0f;
}

static float bmp_comp_press(int32_t adc_P) {
    int64_t v1=(int64_t)bmp_tfine-128000;
    int64_t v2=v1*v1*(int64_t)bmpTrim.P6+(v1*(int64_t)bmpTrim.P5<<17)+((int64_t)bmpTrim.P4<<35);
    v1=((v1*v1*(int64_t)bmpTrim.P3>>8)+(v1*(int64_t)bmpTrim.P2<<12));
    v1=((((int64_t)1<<47)+v1)*(int64_t)bmpTrim.P1)>>33;
    if (!v1) return 0;
    int64_t p=((1048576-adc_P)<<31-v2)*3125/v1;
    v1=((int64_t)bmpTrim.P9*(p>>13)*(p>>13))>>25;
    v2=((int64_t)bmpTrim.P8*p)>>19;
    p=((p+v1+v2)>>8)+((int64_t)bmpTrim.P7<<4);
    return (float)p/25600.0f;
}

static float bmp_comp_hum(int32_t adc_H) {
    int32_t v=bmp_tfine-76800;
    v=(((adc_H<<14)-((int32_t)bmpTrim.H4<<20)-((int32_t)bmpTrim.H5*v)+16384)>>15)*
      (((((v*(int32_t)bmpTrim.H6>>10)*(((v*(int32_t)bmpTrim.H3>>11)+32768))>>10)+2097152)*(int32_t)bmpTrim.H2+8192)>>14);
    v-=(((v>>15)*(v>>15))>>7)*(int32_t)bmpTrim.H1>>4;
    v=v<0?0:v>419430400?419430400:v;
    return (float)(v>>12)/1024.0f;
}

static void bmp_write_config(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    int pi=0;
    uint8_t ot=det.params[pi++].idx, op=det.params[pi++].idx;
    uint8_t oh=bmpTrim.is_bme?det.params[pi++].idx:0;
    uint8_t fi=det.params[pi++].idx, ts=det.params[pi++].idx;
    wr(bus,addr,0xF4,(ot<<5)|(op<<2)|0);
    if(bmpTrim.is_bme) wr(bus,addr,0xF2,oh&7);
    wr(bus,addr,0xF5,(ts<<5)|(fi<<2));
    wr(bus,addr,0xF4,(ot<<5)|(op<<2)|3);
}

static void bmp_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    bmp_load_trim(addr,bus);
    wr(bus,addr,0xE0,0xB6); delay(15);
    if(bmpTrim.is_bme) wr(bus,addr,0xF2,0x03);
    wr(bus,addr,0xF5,(0<<5)|(2<<2));
    wr(bus,addr,0xF4,(2<<5)|(3<<2)|3);
    delay(50);
    det.num_params=0;
    auto add=[&](const char* nm, const char** v, int nv, int def){
        auto& p=det.params[det.num_params++];
        strncpy(p.name,nm,19); p.name[19]=0;
        memcpy(p.vals,v,nv*sizeof(const char*));
        p.num_vals=nv; p.idx=def; p.ro=false;
    };
    add("Oversamp. Temp",  kBmpOsrs,6,2);
    add("Oversamp. Press", kBmpOsrs,6,3);
    if(bmpTrim.is_bme) add("Oversamp. Hum",kBmpOsrs,6,3);
    add("IIR Filter",      kBmpFilt,5,2);
    add("Standby Time",    kBmpTsb, 8,0);
    snprintf(det.note,sizeof(det.note),bmpTrim.is_bme?"BME280: Temp+Press+Hum":"BMP280: Temp+Press only");
}

static void bmp_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    uint8_t d[8];
    if(!rd(bus,addr,0xF7,d,bmpTrim.is_bme?8:6)){det.read_ok=false;return;}
    int32_t aP=((int32_t)d[0]<<12)|((int32_t)d[1]<<4)|(d[2]>>4);
    int32_t aT=((int32_t)d[3]<<12)|((int32_t)d[4]<<4)|(d[5]>>4);
    float T=bmp_comp_temp(aT), P=bmp_comp_press(aP);
    det.num_readings=0;
    auto& r0=det.readings[det.num_readings++]; strncpy(r0.label,"Temp",13);     snprintf(r0.value,23,"%.2f C",T);
    auto& r1=det.readings[det.num_readings++]; strncpy(r1.label,"Pressure",13); snprintf(r1.value,23,"%.1f hPa",P/100.0f);
    if(bmpTrim.is_bme){
        int32_t aH=((int32_t)d[6]<<8)|d[7];
        auto& r2=det.readings[det.num_readings++]; strncpy(r2.label,"Humidity",13);
        snprintf(r2.value,23,"%.1f %%",bmp_comp_hum(aH));
    }
    det.read_ok=true;
}

// ─── MPU-6050 ─────────────────────────────────────────────────────────────────

static const char* kMpuAfs[] = {"+-2g","+-4g","+-8g","+-16g"};
static const char* kMpuGfs[] = {"250/s","500/s","1000/s","2000/s"};
static const char* kMpuDlpf[]= {"260Hz","184Hz","94Hz","44Hz","21Hz","10Hz","5Hz"};
static const float kAfsS[]   = {16384,8192,4096,2048};
static const float kGfsS[]   = {131,65.5f,32.8f,16.4f};

static void mpu_write_config(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    wr(bus,addr,0x1C,det.params[0].idx<<3);
    wr(bus,addr,0x1B,det.params[1].idx<<3);
    wr(bus,addr,0x1A,det.params[2].idx&7);
}

static void mpu_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    wr(bus,addr,0x6B,0x00); delay(100);
    det.num_params=0;
    auto add=[&](const char* nm, const char** v, int nv){
        auto& p=det.params[det.num_params++];
        strncpy(p.name,nm,19); p.name[19]=0;
        memcpy(p.vals,v,nv*sizeof(const char*));
        p.num_vals=nv; p.idx=0; p.ro=false;
    };
    add("Accel Range",kMpuAfs,4);
    add("Gyro Range", kMpuGfs,4);
    add("DLPF BW",    kMpuDlpf,7);
    snprintf(det.note,sizeof(det.note),"MPU-6050: Accel+Gyro+Temp");
}

static void mpu_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    uint8_t d[14];
    if(!rd(bus,addr,0x3B,d,14)){det.read_ok=false;return;}
    float af=kAfsS[det.params[0].idx], gf=kGfsS[det.params[1].idx];
    int16_t ax=(int16_t)(d[0]<<8|d[1]),ay=(int16_t)(d[2]<<8|d[3]),az=(int16_t)(d[4]<<8|d[5]);
    int16_t tx=(int16_t)(d[6]<<8|d[7]);
    int16_t gx=(int16_t)(d[8]<<8|d[9]),gy=(int16_t)(d[10]<<8|d[11]),gz=(int16_t)(d[12]<<8|d[13]);
    det.num_readings=0;
    auto& r0=det.readings[det.num_readings++]; strncpy(r0.label,"Accel X/Y/Z",13);
    snprintf(r0.value,23,"%.2f/%.2f/%.2f g",ax/af,ay/af,az/af);
    auto& r1=det.readings[det.num_readings++]; strncpy(r1.label,"Gyro X/Y/Z",13);
    snprintf(r1.value,23,"%d/%d/%d /s",(int)(gx/gf),(int)(gy/gf),(int)(gz/gf));
    auto& r2=det.readings[det.num_readings++]; strncpy(r2.label,"Temp",13);
    snprintf(r2.value,23,"%.1f C",tx/340.0f+36.53f);
    det.read_ok=true;
}

// ─── AHT20 / AHT21 ───────────────────────────────────────────────────────────

static void aht20_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    delay(40);
    bus.beginTransmission(addr); bus.write(0xBE); bus.write(0x08); bus.write(0x00); bus.endTransmission();
    delay(10);
    det.num_params=0;
    snprintf(det.note,sizeof(det.note),"AHT20/21: Temp+Hum, no config");
}

static void aht20_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    bus.beginTransmission(addr); bus.write(0xAC); bus.write(0x33); bus.write(0x00); bus.endTransmission();
    delay(80);
    bus.requestFrom(addr,(uint8_t)6);
    uint8_t d[6]; for(int i=0;i<6;i++) d[i]=bus.available()?bus.read():0;
    if(d[0]&0x80){det.read_ok=false;return;}
    uint32_t rh=((uint32_t)d[1]<<12)|((uint32_t)d[2]<<4)|(d[3]>>4);
    uint32_t rt=((uint32_t)(d[3]&0x0F)<<16)|((uint32_t)d[4]<<8)|d[5];
    float H=rh*100.0f/1048576.0f, T=rt*200.0f/1048576.0f-50.0f;
    det.num_readings=0;
    auto& r0=det.readings[det.num_readings++]; strncpy(r0.label,"Temp",13);     snprintf(r0.value,23,"%.2f C",T);
    auto& r1=det.readings[det.num_readings++]; strncpy(r1.label,"Humidity",13); snprintf(r1.value,23,"%.1f %%",H);
    det.read_ok=true;
}

// ─── SHT30 / SHT31 ───────────────────────────────────────────────────────────

static const char*   kShtRpt[]  = {"High","Medium","Low"};
static const char*   kShtHeat[] = {"Off","On"};
static const uint8_t kShtCmd[3][2] = {{0x2C,0x06},{0x2C,0x0D},{0x2C,0x10}};
static const uint8_t kShtHeatOn[2] = {0x30,0x6D}, kShtHeatOff[2] = {0x30,0x66};

static void sht3x_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    bus.beginTransmission(addr); bus.write(0x30); bus.write(0xA2); bus.endTransmission(); delay(15);
    det.num_params=0;
    auto& p0=det.params[det.num_params++]; strncpy(p0.name,"Repeatability",19);
    memcpy(p0.vals,kShtRpt,3*sizeof(const char*)); p0.num_vals=3; p0.idx=0; p0.ro=false;
    auto& p1=det.params[det.num_params++]; strncpy(p1.name,"Heater",19);
    memcpy(p1.vals,kShtHeat,2*sizeof(const char*)); p1.num_vals=2; p1.idx=0; p1.ro=false;
    snprintf(det.note,sizeof(det.note),"SHT30/31: Temp+Hum with CRC");
}

static void sht3x_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    uint8_t rpt=det.params[0].idx;
    bus.beginTransmission(addr); bus.write(kShtCmd[rpt][0]); bus.write(kShtCmd[rpt][1]); bus.endTransmission();
    delay(rpt==0?15:rpt==1?6:4);
    bus.requestFrom(addr,(uint8_t)6);
    uint8_t d[6]; for(int i=0;i<6;i++) d[i]=bus.available()?bus.read():0;
    float T=-45.0f+175.0f*(float)((uint16_t)d[0]<<8|d[1])/65535.0f;
    float H=100.0f*(float)((uint16_t)d[3]<<8|d[4])/65535.0f;
    det.num_readings=0;
    auto& r0=det.readings[det.num_readings++]; strncpy(r0.label,"Temp",13);     snprintf(r0.value,23,"%.2f C",T);
    auto& r1=det.readings[det.num_readings++]; strncpy(r1.label,"Humidity",13); snprintf(r1.value,23,"%.1f %%",H);
    det.read_ok=true;
}

static void sht3x_set_param(uint8_t addr, TwoWire& bus, SensorDetail& det, int pidx) {
    if (pidx == 1) {
        const uint8_t* c = det.params[1].idx ? kShtHeatOn : kShtHeatOff;
        bus.beginTransmission(addr); bus.write(c[0]); bus.write(c[1]); bus.endTransmission();
    }
}

// ─── BH1750 ──────────────────────────────────────────────────────────────────

static const char*   kBhMode[] = {"HR 1lx","HR 0.5lx","LR 4lx"};
static const uint8_t kBhCmd[]  = {0x10,0x11,0x13};

static void bh1750_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    bus.beginTransmission(addr); bus.write(0x01); bus.endTransmission(); delay(10);
    det.num_params=0;
    auto& p=det.params[det.num_params++]; strncpy(p.name,"Mode",19);
    memcpy(p.vals,kBhMode,3*sizeof(const char*)); p.num_vals=3; p.idx=0; p.ro=false;
    snprintf(det.note,sizeof(det.note),"BH1750: Ambient light sensor");
}

static void bh1750_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    uint8_t m=det.params[0].idx;
    bus.beginTransmission(addr); bus.write(kBhCmd[m]); bus.endTransmission();
    delay(m<2?180:24);
    bus.requestFrom(addr,(uint8_t)2);
    uint8_t h=bus.available()?bus.read():0, l=bus.available()?bus.read():0;
    float lux=(m==1)?((uint16_t)h<<8|l)/2.0f:((uint16_t)h<<8|l)/1.2f;
    det.num_readings=0;
    auto& r=det.readings[det.num_readings++]; strncpy(r.label,"Light",13); snprintf(r.value,23,"%.1f lux",lux);
    det.read_ok=true;
}

// ─── DHT12 (I2C) ─────────────────────────────────────────────────────────────

static void dht12_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    (void)addr; (void)bus;
    det.num_params=0;
    snprintf(det.note,sizeof(det.note),"DHT12: Temp+Hum, I2C variant");
}

static void dht12_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    bus.beginTransmission(addr); bus.write(0x00);
    if(bus.endTransmission()!=0){det.read_ok=false;return;}
    bus.requestFrom(addr,(uint8_t)5);
    uint8_t d[5]; for(int i=0;i<5;i++) d[i]=bus.available()?bus.read():0;
    if((uint8_t)(d[0]+d[1]+d[2]+d[3])!=d[4]){det.read_ok=false;return;}
    float H=d[0]+d[1]*0.1f, T=d[2]+(d[3]&0x7F)*0.1f;
    if(d[3]&0x80) T=-T;
    det.num_readings=0;
    auto& r0=det.readings[det.num_readings++]; strncpy(r0.label,"Temp",13);     snprintf(r0.value,23,"%.1f C",T);
    auto& r1=det.readings[det.num_readings++]; strncpy(r1.label,"Humidity",13); snprintf(r1.value,23,"%.1f %%",H);
    det.read_ok=true;
}

// ─── Generic (raw registers 0x00..0x0F) ──────────────────────────────────────

static void generic_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    (void)addr; (void)bus;
    det.num_params=0;
    snprintf(det.note,sizeof(det.note),"Unknown \x14 raw regs 0x00..0x0F");
}

static void generic_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    uint8_t buf[16]={};
    rd(bus,addr,0x00,buf,16);
    det.num_readings=0;
    for(int row=0;row<4;row++){
        auto& r=det.readings[det.num_readings++];
        snprintf(r.label,13,"Reg %02Xh",row*4);
        snprintf(r.value,23,"%02X %02X %02X %02X",buf[row*4],buf[row*4+1],buf[row*4+2],buf[row*4+3]);
    }
    det.read_ok=true;
}

// ─── Classify ─────────────────────────────────────────────────────────────────

static int classify(uint8_t addr) {
    if(addr==0x76||addr==0x77) return 1;
    if(addr==0x68||addr==0x69) return 2;
    if(addr==0x38)             return 3;
    if(addr==0x44||addr==0x45) return 4;
    if(addr==0x23||addr==0x5C) return 5;
    if(addr==0x27)             return 6;
    return 0;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void sensor_init(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    memset(&det, 0, sizeof(det));
    switch(classify(addr)){
    case 1: bmp_init(addr,bus,det);    break;
    case 2: mpu_init(addr,bus,det);    break;
    case 3: aht20_init(addr,bus,det);  break;
    case 4: sht3x_init(addr,bus,det);  break;
    case 5: bh1750_init(addr,bus,det); break;
    case 6: dht12_init(addr,bus,det);  break;
    default:generic_init(addr,bus,det);break;
    }
}

void sensor_read(uint8_t addr, TwoWire& bus, SensorDetail& det) {
    switch(classify(addr)){
    case 1: bmp_read(addr,bus,det);    break;
    case 2: mpu_read(addr,bus,det);    break;
    case 3: aht20_read(addr,bus,det);  break;
    case 4: sht3x_read(addr,bus,det);  break;
    case 5: bh1750_read(addr,bus,det); break;
    case 6: dht12_read(addr,bus,det);  break;
    default:generic_read(addr,bus,det);break;
    }
}

void sensor_set_param(uint8_t addr, TwoWire& bus, SensorDetail& det, int pidx, int delta) {
    if(pidx<0||pidx>=det.num_params) return;
    auto& p=det.params[pidx];
    if(p.ro||!p.num_vals) return;
    p.idx=(uint8_t)((p.idx+delta+p.num_vals)%p.num_vals);
    switch(classify(addr)){
    case 1: bmp_write_config(addr,bus,det); break;
    case 2: mpu_write_config(addr,bus,det); break;
    case 4: sht3x_set_param(addr,bus,det,pidx); break;
    default: break;
    }
}
