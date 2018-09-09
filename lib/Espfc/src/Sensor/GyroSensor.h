#ifndef _ESPFC_SENSOR_GYRO_SENSOR_H_
#define _ESPFC_SENSOR_GYRO_SENSOR_H_

#include "BaseSensor.h"
#include "Device/GyroDevice.h"

#include <math.h>

#define ESPFC_FUZZY_ACCEL_ZERO 0.05
#define ESPFC_FUZZY_GYRO_ZERO 0.20

namespace Espfc {

namespace Sensor {

class GyroSensor: public BaseSensor
{
  public:
    GyroSensor(Model& model): _model(model) {}

    int begin()
    {
      _gyro = Hardware::getGyroDevice(_model);
      if(!_gyro) return 0;

      _gyro->setDLPFMode(_model.config.gyroDlpf);
      _gyro->setRate(_model.state.gyroDivider - 1);

      switch(_model.config.gyroFsr)
      {
        case GYRO_FS_2000: _model.state.gyroScale = M_PI /  (16.384 * 180.0); break;
        case GYRO_FS_1000: _model.state.gyroScale = M_PI /  (32.768 * 180.0); break;
        case GYRO_FS_500:  _model.state.gyroScale = M_PI /  (65.535 * 180.0); break;
        case GYRO_FS_250:  _model.state.gyroScale = M_PI / (131.072 * 180.0); break;
      }
      _gyro->setFullScaleGyroRange(_model.config.gyroFsr);

      _model.logger.info().log(F("GYRO INIT")).log(_model.config.gyroDlpf).log(_model.state.gyroDivider).log(_model.state.gyroTimer.rate).log(_model.state.gyroTimer.interval).log(_model.config.gyroDev).logln(_model.state.gyroPresent);

      return 1;
    }

    int update()
    {
      if(!_model.gyroActive()) return 0;
      if(!_model.state.gyroTimer.check()) return 0;
      //return 1;

      {
        Stats::Measure measure(_model.state.stats, COUNTER_GYRO_READ);
        _gyro->readGyro(_model.state.gyroRaw);
      }

      {
        Stats::Measure measure(_model.state.stats, COUNTER_GYRO_FILTER);

        if(!_model.gyroActive()) return 0;

        align(_model.state.gyroRaw, _model.config.gyroAlign);

        _model.state.gyro = (VectorFloat)_model.state.gyroRaw  * _model.state.gyroScale;
        if(_model.state.gyroBiasSamples > 0) // calibration
        {
          VectorFloat deltaAccel = _model.state.accel - _model.state.accelPrev;
          _model.state.accelPrev = _model.state.accel;
          if(deltaAccel.getMagnitude() < ESPFC_FUZZY_ACCEL_ZERO && _model.state.gyro.getMagnitude() < ESPFC_FUZZY_GYRO_ZERO)
          {
            _model.state.gyroBias += (_model.state.gyro - _model.state.gyroBias) * _model.state.gyroBiasAlpha;
            _model.state.gyroBiasSamples--;
            if(_model.state.gyroBiasSamples == 0)
            {
              _model.state.buzzer.push(BEEPER_GYRO_CALIBRATED);
              _model.logger.info().log(F("GYRO CAL")).log(_model.state.gyroBias.x).log(_model.state.gyroBias.y).logln(_model.state.gyroBias.z);
            }
          }
        }
        _model.state.gyro -= _model.state.gyroBias;

        // filtering
        for(size_t i = 0; i < 3; ++i)
        {
          if(_model.config.debugMode == DEBUG_GYRO)
          {
            _model.state.debug[i] = _model.state.gyroRaw[i];
          }
          if(_model.config.debugMode == DEBUG_NOTCH)
          {
            _model.state.debug[i] = lrintf(degrees(_model.state.gyro[i]));
          }

          // dynamic filter start
          bool dynamicFilter = _model.isActive(FEATURE_DYNAMIC_FILTER);
          if(dynamicFilter || _model.config.debugMode == DEBUG_FFT_FREQ)
          {
            _model.state.gyroAnalyzer[i].update(_model.state.gyro[i]);
            if(_model.config.debugMode == DEBUG_FFT_FREQ)
            {
              _model.state.debug[i] = _model.state.gyroAnalyzer[i].freq;
              if(i == 0) _model.state.debug[3] = lrintf(degrees(_model.state.gyro[0]));
            }
          }
          if(dynamicFilter)
          {
            if((_model.state.gyroTimer.iteration & 0x1f) == 0) // every 8th (0x07) sample
            {
              _model.state.gyroDynamicFilter[i].reconfigureNotchDF1(_model.state.gyroAnalyzer[i].freq, _model.state.gyroAnalyzer[i].cutoff);
            }
            _model.state.gyro.set(i, _model.state.gyroDynamicFilter[i].update(_model.state.gyro[i]));
          }
          // dynamic filter end

          _model.state.gyro.set(i, _model.state.gyroNotch1Filter[i].update(_model.state.gyro[i]));
          _model.state.gyro.set(i, _model.state.gyroNotch2Filter[i].update(_model.state.gyro[i]));
          _model.state.gyro.set(i, _model.state.gyroFilter[i].update(_model.state.gyro[i]));
          _model.state.gyro.set(i, _model.state.gyroFilter2[i].update(_model.state.gyro[i]));
          _model.state.gyro.set(i, _model.state.gyroFilter3[i].update(_model.state.gyro[i]));
          if(_model.accelActive())
          {
            _model.state.gyroImu.set(i, _model.state.gyroFilterImu[i].update(_model.state.gyro[i]));
          }
        }
      }     

      return 1;
    }

  private:
    Model& _model;
    Device::GyroDevice * _gyro;
};

}

}
#endif