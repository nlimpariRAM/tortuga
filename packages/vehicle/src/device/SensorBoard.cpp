/*
 * Copyright (C) 2008 Robotics at Maryland
 * Copyright (C) 2008 Joseph Lisee <jlisee@umd.edu>
 * All rights reserved.
 *
 * Author: Joseph Lisee <jlisee@umd.edu>
 * File:  packages/vision/src/device/SensorBoard.cpp
 */
//#include <iostream>
// Library Includes
#include <log4cpp/Category.hh>

// Project Includes
#include "vehicle/include/device/SensorBoard.h"
#include "vehicle/include/Events.h"

#include "math/include/Events.h"


RAM_CORE_EVENT_TYPE(ram::vehicle::device::SensorBoard, POWERSOURCE_UPDATE);
RAM_CORE_EVENT_TYPE(ram::vehicle::device::SensorBoard, TEMPSENSOR_UPDATE);
RAM_CORE_EVENT_TYPE(ram::vehicle::device::SensorBoard, THRUSTER_UPDATE);
RAM_CORE_EVENT_TYPE(ram::vehicle::device::SensorBoard, SONAR_UPDATE);

static log4cpp::Category& LOGGER(log4cpp::Category::getInstance("SensorBoard"));

namespace ram {
namespace vehicle {
namespace device {

SensorBoard::SensorBoard(int deviceFD,
                         core::ConfigNode config,
                         core::EventHubPtr eventHub) :
    Device(config["name"].asString()),
    m_depthCalibSlope(config["depthCalibSlope"].asDouble()),
    m_depthCalibIntercept(config["depthCalibIntercept"].asDouble()),
    m_deviceFile(""),
    m_deviceFD(deviceFD)
{
    m_state.thrusterValues[0] = 0;
    m_state.thrusterValues[1] = 0;
    m_state.thrusterValues[2] = 0;
    m_state.thrusterValues[3] = 0;
    m_state.thrusterValues[4] = 0;
    m_state.thrusterValues[5] = 0;
    // If we get a negative FD, don't try to talk to the board
    if (deviceFD >= 0)
        establishConnection();

    // Log file header
    LOGGER.info("% MC1 MC2 MC3 MC4 MC5 MC6 TV1 TV2 TV3 TV4 TV5 TV6 TimeStamp");
}
    

SensorBoard::SensorBoard(core::ConfigNode config,
                         core::EventHubPtr eventHub,
                         IVehiclePtr vehicle) :
    Device(config["name"].asString()),
    m_depthCalibSlope(config["depthCalibSlope"].asDouble()),
    m_depthCalibIntercept(config["depthCalibIntercept"].asDouble()),
    m_deviceFile(config["deviceFile"].asString("/dev/sensor")),
    m_deviceFD(-1)
{
    establishConnection();
    m_state.thrusterValues[0] = 0;
    m_state.thrusterValues[1] = 0;
    m_state.thrusterValues[2] = 0;
    m_state.thrusterValues[3] = 0;
    m_state.thrusterValues[4] = 0;
    m_state.thrusterValues[5] = 0;

    for (int i = 0; i < 11; ++i)
        update(1.0/40);

    // Log file header
    /*std::cout << "I HAVE '" << LOGGER.getName() << "' THIS MANY APPENDERS: " << LOGGER.getAllAppenders().size() << std::endl;
    
    std::vector< log4cpp::Category * > * cat = log4cpp::Category::getCurrentCategories();
    for (size_t i = 0; i < cat->size(); ++i)
    {
       if ((*cat)[i] == &LOGGER)
       {
          std::cout << "MY CAT! ";
       }
       std::cout << "Name: '" << (*cat)[i]->getName() << "' Appends #"
       << (*cat)[i]->getAllAppenders().size() << std::endl;
       log4cpp::AppenderSet appenders = (*cat)[i]->getAllAppenders();
       log4cpp::AppenderSet::iterator iter = appenders.begin();
       while(iter != appenders.end())
       {
           std::cout << "\t Name: '" << (*iter)->getName() << "'" << std::endl;
           iter++;
       }
    }l*/
//    std::cout << "Cats: " << log4cpp::Category::getCurrentCategories()->size() << std::endl;
    LOGGER.info("% MC1 MC2 MC3 MC4 MC5 MC6 TV1 TV2 TV3 TV4 TV5 TV6 TimeStamp");
}
    
SensorBoard::~SensorBoard()
{
    Updatable::unbackground(true);

    boost::mutex::scoped_lock lock(m_deviceMutex);
    if (m_deviceFD >= 0)
    {
        close(m_deviceFD);
        m_deviceFD = -1;
    }
}

void SensorBoard::update(double timestep)
{
    // Copy the values to local state
    VehicleState state;
    {
        core::ReadWriteMutex::ScopedReadLock lock(m_stateMutex);
        state = m_state;
    }

    {
        boost::mutex::scoped_lock lock(m_deviceMutex);
    
        // Send commands
        setSpeeds(state.thrusterValues[0],
                  state.thrusterValues[1],
                  state.thrusterValues[2],
                  state.thrusterValues[3],
                  state.thrusterValues[4],
                  state.thrusterValues[5]);
    
        // Do a partial read
        int ret = partialRead(&state.telemetry);
        if (ret == SB_UPDATEDONE)
        {
            powerSourceEvents(&state.telemetry);
            tempSensorEvents(&state.telemetry);
            thrusterEvents(&state.telemetry);
            sonarEvent(&state.telemetry);
            
            LOGGER.info("%3.1f %3.1f %3.1f %3.1f %3.1f %3.1f"
                        " %d %d %d %d %d %d",
                        state.telemetry.powerInfo.motorCurrents[0],
                        state.telemetry.powerInfo.motorCurrents[1],
                        state.telemetry.powerInfo.motorCurrents[2],
                        state.telemetry.powerInfo.motorCurrents[3],
                        state.telemetry.powerInfo.motorCurrents[4],
                        state.telemetry.powerInfo.motorCurrents[5],
                        state.thrusterValues[0],
                        state.thrusterValues[1],
                        state.thrusterValues[2],
                        state.thrusterValues[3],
                        state.thrusterValues[4],
                        state.thrusterValues[5]);
        }
    
        // Now read depth
        ret = readDepth();
        double depth = (((double)ret) - m_depthCalibIntercept) /
            m_depthCalibSlope; 
         state.depth = depth;
    } // end deviceMutex lock
    
    // Copy the values back
    {
        core::ReadWriteMutex::ScopedWriteLock lock(m_stateMutex);
        m_state = state;
    }
}

double SensorBoard::getDepth()
{
    core::ReadWriteMutex::ScopedReadLock lock(m_stateMutex);
    return m_state.depth;
}

void SensorBoard::setThrusterValue(int address, int count)
{
    assert((0 <= address) && (address < 6) && "Address out of range");
    core::ReadWriteMutex::ScopedWriteLock lock(m_stateMutex);
    m_state.thrusterValues[address] = count;
}

bool SensorBoard::isThrusterEnabled(int address)
{
    static int addressToEnable[] = {
        THRUSTER1_ENABLED,
        THRUSTER2_ENABLED,
        THRUSTER3_ENABLED,
        THRUSTER4_ENABLED,
        THRUSTER5_ENABLED,
        THRUSTER6_ENABLED
    };

    assert((0 <= address) && (address < 6) && "Address out of range");

    core::ReadWriteMutex::ScopedReadLock lock(m_stateMutex);
    return (0 != (addressToEnable[address] & m_state.telemetry.thrusterState));
}

void SensorBoard::setThrusterEnable(int address, bool state)
{
    static int addressToOn[] = {
        CMD_THRUSTER1_ON,
        CMD_THRUSTER2_ON,
        CMD_THRUSTER3_ON,
        CMD_THRUSTER4_ON,
        CMD_THRUSTER5_ON,
        CMD_THRUSTER6_ON
    };
    
    static int addressToOff[] = {
        CMD_THRUSTER1_OFF,
        CMD_THRUSTER2_OFF,
        CMD_THRUSTER3_OFF,
        CMD_THRUSTER4_OFF,
        CMD_THRUSTER5_OFF,
        CMD_THRUSTER6_OFF
    };

    assert((0 <= address) && (address < 6) && "Address out of range");

    {
        boost::mutex::scoped_lock lock(m_deviceMutex);

        int val;
        
        if (state)
            val = addressToOn[address];
        else
            val = addressToOff[address];
        
        setThrusterSafety(val);
    }
    
    // Now set our internal flag to make everything consistent (maybe)
}


bool SensorBoard::isPowerSourceEnabled(int address)
{
    static int addressToEnable[] = {
        BATT1_ENABLED,
        BATT2_ENABLED,
        BATT3_ENABLED,
        BATT4_ENABLED,
        BATT5_ENABLED,
    };

    assert((0 <= address) && (address < 5) && "Address out of range");

    core::ReadWriteMutex::ScopedReadLock lock(m_stateMutex);
    return (0 != (addressToEnable[address] & m_state.telemetry.battEnabled));
}

bool SensorBoard::isPowerSourceInUse(int address)
{
    static int addressToEnable[] = {
        BATT1_INUSE,
        BATT2_INUSE,
        BATT3_INUSE,
        BATT4_INUSE,
        BATT5_INUSE,
    };

    assert((0 <= address) && (address < 5) && "Address out of range");

    core::ReadWriteMutex::ScopedReadLock lock(m_stateMutex);
    return (0 != (addressToEnable[address] & m_state.telemetry.battUsed));
}

void SensorBoard::setPowerSouceEnabled(int address, bool state)
{
    static int addressToOn[] = {
        CMD_BATT1_ON,
        CMD_BATT2_ON,
        CMD_BATT3_ON,
        CMD_BATT4_ON,
        CMD_BATT5_ON,
    };
    
    static int addressToOff[] = {
        CMD_BATT1_OFF,
        CMD_BATT2_OFF,
        CMD_BATT3_OFF,
        CMD_BATT4_OFF,
        CMD_BATT5_OFF,
    };

    assert((0 <= address) && (address < 5) && "Power source id out of range");

    {
        boost::mutex::scoped_lock lock(m_deviceMutex);

        int val;
        
        if (state)
            val = addressToOn[address];
        else
            val = addressToOff[address];
        
        setBatteryState(val);
    }
}

void SensorBoard::dropMarker()
{
    static int markerNum = 0;
    boost::mutex::scoped_lock lock(m_deviceMutex);
    
    if (markerNum <= 1)
    {
        dropMarker(markerNum);
        markerNum++;
    }
    else
    {
        // Report an error
    }
}
    
void SensorBoard::setSpeeds(int s1, int s2, int s3, int s4, int s5, int s6)
{
    handleReturn(::setSpeeds(m_deviceFD, s1, s2, s3, s4, s5, s6));
}
    
int SensorBoard::partialRead(struct boardInfo* telemetry)
{
    int ret = ::partialRead(m_deviceFD, telemetry);
    if (handleReturn(ret))
        return ret;
    else
        assert(false && "Can't talk to sensor board");
    return 0;
}

int SensorBoard::readDepth()
{
    int ret = ::readDepth(m_deviceFD);
    if (handleReturn(ret))
        return ret;
    else
        assert(false && "Can't talk to sensor board");
    return 0;
}

void SensorBoard::setThrusterSafety(int state)
{
    handleReturn(::setThrusterSafety(m_deviceFD, state));
}

void SensorBoard::setBatteryState(int state)
{
    handleReturn(::setBatteryState(m_deviceFD, state));
}

void SensorBoard::dropMarker(int markerNum)
{
    handleReturn(::dropMarker(m_deviceFD, markerNum));
}

void SensorBoard::syncBoard()
{
    if (SB_ERROR == ::syncBoard(m_deviceFD))
    {
        assert(false && "Can't sync with the sensor board");
    }
}

void SensorBoard::establishConnection()
{
    boost::mutex::scoped_lock lock(m_deviceMutex);
    if (m_deviceFD < 0)
    {
        m_deviceFD = openSensorBoard(m_deviceFile.c_str());

        if (m_deviceFD < 0)
        {
            assert(false && "Can't open sensor board file");
        }
    }

    syncBoard();
}

bool SensorBoard::handleReturn(int ret)
{
    if (ret < 0)
    {
        close(m_deviceFD);
        m_deviceFD = -1;
        establishConnection();
    }

    return true;
}

void SensorBoard::powerSourceEvents(struct boardInfo* telemetry)
{
    static int id2Enable[5] = {
        BATT1_ENABLED,
        BATT2_ENABLED,
        BATT3_ENABLED,
        BATT4_ENABLED,
        BATT5_ENABLED
    };

    static int id2InUse[5] = {
        BATT1_INUSE,
        BATT2_INUSE,
        BATT3_INUSE,
        BATT4_INUSE,
        BATT5_INUSE
    };
    
    for (int i = BATTERY_1; i <= SHORE; ++i)
    {
        PowerSourceEventPtr event(new PowerSourceEvent);
        event->id = i;
        event->enabled = telemetry->battEnabled & id2Enable[i];
        event->inUse = telemetry->battUsed & id2InUse[i];
        event->voltage = telemetry->powerInfo.battVoltages[i];
        event->current = telemetry->powerInfo.battCurrents[i];
        publish(POWERSOURCE_UPDATE, event);
    }
}

void SensorBoard::tempSensorEvents(struct boardInfo* telemetry)
{
    for (int i = 0; i < NUM_TEMP_SENSORS; ++i)
    {
        TempSensorEventPtr event(new TempSensorEvent);
        event->id = i;
        event->temp = telemetry->temperature[i];
        publish(TEMPSENSOR_UPDATE, event);
    }
}
    
void SensorBoard::thrusterEvents(struct boardInfo* telemetry)
{
    static int addressToEnable[] = {
        THRUSTER1_ENABLED,
        THRUSTER2_ENABLED,
        THRUSTER3_ENABLED,
        THRUSTER4_ENABLED,
        THRUSTER5_ENABLED,
        THRUSTER6_ENABLED
    };
    
    for (int i = 0; i < 6; ++i)
    {
        ThrusterEventPtr event(new ThrusterEvent);
        event->address = i;
        event->current = telemetry->powerInfo.motorCurrents[i];
        event->enabled = telemetry->thrusterState & addressToEnable[i]; 
        publish(THRUSTER_UPDATE, event);
    }
}

void SensorBoard::sonarEvent(struct boardInfo* telemetry)
{
    SonarEventPtr event(new SonarEvent);
    event->direction = math::Vector3(telemetry->sonar.vectorX,
                                     telemetry->sonar.vectorY,
                                     telemetry->sonar.vectorZ);
    event->range = telemetry->sonar.range;
    event->pingTimeSec = telemetry->sonar.timeStampSec;
    event->pingTimeUSec = telemetry->sonar.timeStampUSec;

    publish(SONAR_UPDATE, event);
}
    
} // namespace device
} // namespace vehicle
} // namespace ram
