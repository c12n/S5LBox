/* Shared window types, derived from touchHLE's window.rs.
 * Mozilla Public License, v. 2.0: https://mozilla.org/MPL/2.0/ */
use std::collections::HashMap;

#[allow(non_camel_case_types)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum DeviceFamily {
    iPhone,
    iPad,
}
impl std::fmt::Display for DeviceFamily {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}
impl DeviceFamily {
    pub fn portrait_size(&self) -> (u32, u32) {
        match self {
            DeviceFamily::iPhone => (320, 480),
            DeviceFamily::iPad => (768, 1024),
        }
    }
}
impl TryFrom<u64> for DeviceFamily {
    type Error = ();
    fn try_from(value: u64) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(DeviceFamily::iPhone),
            2 => Ok(DeviceFamily::iPad),
            _ => Err(()),
        }
    }
}
impl TryFrom<&str> for DeviceFamily {
    type Error = ();
    fn try_from(value: &str) -> Result<Self, Self::Error> {
        match value {
            "iphone" => Ok(DeviceFamily::iPhone),
            "ipad" => Ok(DeviceFamily::iPad),
            _ => Err(()),
        }
    }
}

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum DeviceOrientation {
    Portrait,
    PortraitUpsideDown,
    LandscapeLeft,
    LandscapeRight,
}

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
pub enum FingerId {
    Mouse,
    Touch(i64),
    VirtualCursor,
    ButtonToTouch(crate::options::Button),
    StickToTouch,
    DpadToTouch,
}
pub type Coords = (f32, f32);

#[derive(Debug)]
pub enum TextInputEvent {
    Text(String),
    Backspace,
    Return,
}

#[derive(Debug)]
pub enum Event {
    /// User requested quit.
    Quit,
    /// OS has informed touchHLE it will soon become inactive.
    /// (iOS `applicationWillResignActive:`, Android `onPause()`)
    AppWillResignActive,
    /// OS has informed touchHLE it will soon terminate.
    /// (iOS `applicationWillTerminate:`, Android `onDestroy()`)
    AppWillTerminate,
    TouchesDown(HashMap<FingerId, Coords>),
    TouchesMove(HashMap<FingerId, Coords>),
    TouchesUp(HashMap<FingerId, Coords>),
    /// User pressed F12, requesting that execution be paused and the debugger
    /// take over.
    EnterDebugger,
    TextInput(TextInputEvent),
}

pub enum BatteryState {
    Unknown,
    OnBattery,
    NoBattery,
    Charging,
    Full,
}

pub enum GLVersion {
    /// OpenGL ES 1.1
    GLES11,
    /// OpenGL 2.1 compatibility profile
    GL21Compat,
}
