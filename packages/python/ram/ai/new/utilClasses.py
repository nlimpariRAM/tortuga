import time
import ext.vision as vision
import ext.vehicle as vehicle
import math as m

#checks if a specified amount of time has passed
#check will return true until duration is exceeded
class Timer(object):
    def __init__(self, duration):
        self.reset()
        self._duration = duration

    def reset(self):
        self._start = time.time()
    
    def check(self):
        return (self._duration > (time.time() - self._start))

class VisionObject(object):
    def __init__(self):
        self.seen = False
        self.x = 0
        self.y = 0
        self.angle = 0
        self.range = 0

    #should be overloaded to update the values in here
    def update(self):
        pass

    def isSeen(self):
        return self.seen

#hack vision object that  tracks a Red buoy in the old simulator
class BuoyVisionObject(VisionObject):
    def __init__(self, oldStatePtr):
        super(BuoyVisionObject,self).__init__()
        oldStatePtr.queuedEventHub.subscribeToType(vision.EventType.BUOY_FOUND,self.callback)
        oldStatePtr.queuedEventHub.subscribeToType(vision.EventType.BUOY_LOST,self.seeit)


    def callback(self,event):
        if(event.color == vision.Color.RED):
            self.seen = True
            self.x = event.x
            self.y = event.y
            self.range = 1/event.range
            
    def seeit(self,event):
        if(event.color == vision.Color.RED):
            self.seen = False

#hack vision object that  tracks a pipe in the old simulator
class PipeVisionObject(VisionObject):
    def __init__(self,oldStatePtr):
        super(PipeVisionObject,self).__init__()
        oldStatePtr.queuedEventHub.subscribeToType(vision.EventType.PIPE_FOUND,self.callback)
        oldStatePtr.queuedEventHub.subscribeToType(vision.EventType.PIPE_LOST,self.seeit)


    def callback(self,event):
        self.seen = True
        self.x = event.x
        self.y = event.y
        self.angle = event.angle.valueDegrees()
            
    def seeit(self,event):
        self.seen = False

class SonarObject(object):
    def __init__(self):
        self.x = 0
        self.y = 0
        self.z = 0
        self.seen = False

    def update(self):
        pass

class OldSimulatorHackSonarObject(SonarObject):
    def __init__(self,oldStatePtr):
        super(OldSimulatorHackSonarObject,self).__init__()
        oldStatePtr.queuedEventHub.subscribeToType(vehicle.device.ISonar.UPDATE,self.callback)

    def callback(self,event):
        self.x = event.direction.x
        self.y = event.direction.y
        self.z = event.direction.z
        self.seen = True
        
#checks if a vision object is in the range specified
class ObjectInVisionRangeQuery(object):
    def __init__(self, visionObject, x_center, y_center, range_center, x_range, y_range, range_range):
        self._obj = visionObject
        self._x_center = x_center
        self._y_center = y_center
        self._range_center = range_center
        self._x_range = x_range
        self._y_range = y_range
        self._range_range = range_range
    
    def query(self):
        self._obj.update()
        obj = self._obj
        return self._obj.seen and ((abs(obj.x - self._x_center) <= self._x_range) and (abs(obj.y - self._y_center) <= self._y_range) and (abs(obj.range - self._range_center) <= self._range_range))

class ObjectInSonarQuery(object):
    def __init__(self, sonarObject, x_center, y_center, z_center, x_range, y_range, z_range):
        self._obj = sonarObject
        self._x_center = x_center
        self._y_center = y_center
        self._z_center = z_center
        self._x_range = x_range
        self._y_range = y_range
        self._z_range = z_range

    def query(self):
        self._obj.update()
        obj = self._obj
        return self._obj.seen and ((abs(obj.x - self._x_center) <= self._x_range) and (abs(obj.y - self._y_center) <= self._y_range))

class ObjectInSonarQueryAngle(object):
    def __init__(self, sonarObject, angle_range):
        self._obj = sonarObject
        self._angle_range = angle_range

    def query(self):
        self._obj.update()
        obj = self._obj
        if (obj.x != 0):
            return (abs(m.degrees(m.atan(obj.y/obj.x))) <= self._angle_range)
        else:
            return False

# this class transforms a query into a  query which only becomes false if the the query has 
# only returned false under a certain amount of time(such that all queries made in that time 
# returned true, does not account for queries that weren't actually made)
# this effectively introduces a delay into a query, it is useful for dealing with things like 
# vision where an object might disappear for 1 or 2 frames, but then reappear immediately afterwards
# TLDR; this query returns if "query" was true in the last "timeout" seconds, as long as you call it continously
class hasQueryBeenFalse(object):
    def __init__(self, timeout, query):
        self._query = query
        self._timer = Timer(timeout)

    def query(self):
        #if query true, reset timer and return true
        if(self._query()):
            self._timer.reset()
            return True
        else:
            #if query false return if the timer has triggered yet
            return self._timer.check()

class hasQueryBeenTrue(object):
    def __init__(self, timeout, query):
        self._query = query
        self._timer = Timer(timeout)

    def query(self):
        #if query false, reset timer and return true
        if(not self._query()):
            self._timer_reset()
            return True
        else:
            #if query true return if the timer as triggered yet
            return self._timer.check()
