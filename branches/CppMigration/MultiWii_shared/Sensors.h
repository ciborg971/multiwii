#ifndef SENSORS_H_
#define SENSORS_H_

#if defined(MMA7455) || defined(MMA8451Q) || defined(ADXL345) || \
    defined(BMA180) || defined(BMA020) || defined(NUNCHACK) || \
    defined(LIS3LV02) || defined(LSM303DLx_ACC) || defined(ADCACC) || \
    defined(MPU6050) || defined(NUNCHUCK)
void ACC_getADC ();
#endif

#if defined(L3G4200D) || defined(ITG3200) || defined(MPU6050) || \
	defined(MPU3050) || defined(WMP) || defined(NUNCHUCK)
void Gyro_getADC ();
#endif

#if MAG
uint8_t Mag_getADC();
#endif

#if defined(BMP085) || defined(MS561101BA)
uint8_t Baro_update();
#endif

void initSensors();

#endif /* SENSORS_H_ */
