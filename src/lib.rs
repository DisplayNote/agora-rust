use cpp::cpp;
use cpp::cpp_class;
use std::ffi::{CString, CStr};
use std::os::raw::c_char;

cpp!{{
    #include "src/cpp/agorasdk/AgoraSdk.h"
    using std::string;
}}

cpp_class!(pub unsafe struct Config as "agora::recording::RecordingConfig");
impl Config {
    fn new() -> Self {
        unsafe { cpp!([] -> Config as "agora::recording::RecordingConfig" {return agora::recording::RecordingConfig();}) }
    }

    fn is_mixing_enabled(&self) -> bool {
        unsafe {
            cpp!([self as "agora::recording::RecordingConfig*"] -> bool as "bool" {
                return self->isMixingEnabled;
            })
        }    
    }

    fn set_mixing_enabled(&self, enabled: bool) {
        println!("set_mixing_enabled:{}", enabled);
        unsafe {
            cpp!([  self as "agora::recording::RecordingConfig*",
                    enabled as "bool"] {
                self->isMixingEnabled = enabled;
            })
        }    
    }

    fn set_recording_path(&self, path: &str) {
        let path = CString::new(path).unwrap().into_raw();
        unsafe {
            cpp!([  self as "agora::recording::RecordingConfig*",
                    path as "const char *"] {
                self->appliteDir = path;
            })
        }
    }

    fn recording_path(&self) -> Result<&str, std::str::Utf8Error> {
        let p = unsafe {
            cpp!([self as "agora::recording::RecordingConfig*"] -> *const c_char as "const char *" {
                return self->appliteDir;
            })
        } as *const i8;
        let c = unsafe {CStr::from_ptr(p)};
        c.to_str()
    }
}

cpp_class!(pub unsafe struct Layout as "agora::linuxsdk::VideoMixingLayout");
impl Layout {
    fn new() -> Self {
        unsafe { cpp!([] -> Layout as "agora::linuxsdk::VideoMixingLayout" {return agora::linuxsdk::VideoMixingLayout();}) }
    }
}

cpp_class!(pub unsafe struct Recorder as "agora::AgoraSdk");
impl Recorder {
    fn new() -> Self {
        unsafe { cpp!([] -> Recorder as "agora::AgoraSdk" {return agora::AgoraSdk();}) }
    }

    fn create_channel(&self, app_id: &str, channel_key: &str, name: &str, uid: u32, config: &Config) -> bool {
        
        let app_id = app_id.as_ptr();
        let name = name.as_ptr();
        let channel_key = channel_key.as_ptr();
        
        unsafe {
            cpp!([  self as "agora::AgoraSdk*", 
                    app_id as "const char *",
                    channel_key as "const char *",
                    name as "const char *",
                    uid as "int",
                    config as "agora::recording::RecordingConfig*"
                    ] -> bool as "bool" {
                        return self->createChannel(app_id, channel_key, name, uid, *config);
                    }

            )
        }
    }
    
    fn update_mix_mode_setting(&self, width: u32, height: u32, is_video_mix: bool) {
        unsafe {
            cpp!([self as "agora::AgoraSdk*",
                width as "int",
                height as "int", 
                is_video_mix as "bool"] {
                    self->updateMixModeSetting(width, height, is_video_mix);
                }

            )
        }
    }

    fn leave_channel(&self) -> bool {
        unsafe {
            cpp!([self as "agora::AgoraSdk*"] -> bool as "bool" {
                    return self->leaveChannel();
                }
            )
        }
    }

    fn set_video_mixing_layout(&self, layout: &Layout) -> u32 {
        unsafe {
            cpp!([  self as "agora::AgoraSdk*", 
                    layout as "agora::linuxsdk::VideoMixingLayout*"] -> u32 as "int" {
                
                return self->setVideoMixingLayout(*layout);
            })
        }        
    }

    fn release(&self) -> bool {
        unsafe {
            cpp!([self as "agora::AgoraSdk*"] -> bool as "bool" {
                return self->release();
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_mixing_enabled() {
        let config = Config::new();
        assert!(!config.is_mixing_enabled(), "should be false from the start");
        config.set_mixing_enabled(true);
        assert!(config.is_mixing_enabled(), "should be true after updating");
    }

    #[test]
    fn config_set_recording_path() {
        let config = Config::new();
        let str = "test";
        config.set_recording_path(str);
        assert!(config.recording_path().is_ok());
        let path = config.recording_path().unwrap();

        assert!(path == str);
    }
}