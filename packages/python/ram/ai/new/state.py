import weakref

import stateMachine

class State(object):
    def __init__(self, name):
        self._name = name
        self._machine = None

        self._transitions = {}      # Map from transition name to weakref of next state
        self._enterCallbacks = {}   # Map from transition name to callback function
        self._leaveCallbacks = {}   # Map from transition name to callback function

    def getName(self):
        '''Returns the name of this state.'''
        return self._name

    def getState(self, stateName):
        '''Returns the state in the parent state machine associated with the given name.'''
        self.getStateMachine().getState(stateName)

    def getStateMachine(self):
        '''Returns the state machine for this state.'''
        return self._machine()

    def setStateMachine(self, machine):
        '''Sets the parent state machine for this state.'''
        self._machine = weakref.ref(machine)

    def getNextState(self, transitionName):
        '''Returns the next state for the given transition.'''
        try:
            return self._transitions[transitionName]()
        except KeyError:
            raise Exception('Transition "' + transitionName + '" in "' + self._name + '" does not exist.')

    def setNextState(self, transitionName, nextState):
        '''Sets the next state for the given transition.'''
        self._transitions[transitionName] = weakref.ref(nextState)

    def getEnterCallback(self, transitionName):
        '''Returns the enter callback for the given transition.'''
        return self._enterCallbacks.get(transitionName, self.enter)

    def setEnterCallback(self, transitionName, callback):
        '''Sets the enter callback for the given transition.'''
        self._enterCallbacks[transitionName] = callback

    def getLeaveCallback(self, transitionName):
        '''Returns the leave callback for the given transition.'''
        return self._leaveCallbacks.get(transitionName, self.leave)

    def setLeaveCallback(self, transitionName, callback):
        '''Sets the leave callback for the given transition.'''
        self._leaveCallbacks[transitionName] = callback

    def doEnter(self, transitionName):
        '''Signals the state to perform its enter callback.'''
        self.getEnterCallback(transitionName)()

    def doLeave(self, transitionName):
        '''Signals the state to perform its leave callback.'''
        self.getLeaveCallback(transitionName)()

    def doTransition(self, transitionName):
        '''Signals the state machine to execute the given transition.'''
        self.getStateMachine().queueTransition(transitionName)

    def configure(self, *args, **kwargs):
        pass

    def enter(self):
        pass

    def update(self):
        pass

    def leave(self):
        pass
