#include <csignal>
#include <cstdint>
#include <iostream>
#include <sstream> 
#include <string>
#include <vector> 
#include <algorithm> 
#include <stdlib.h>
#include "../include/IAgoraLinuxSdkCommon.h"
#include "../include/IAgoraRecordingEngine.h"
#include "AgoraSdk.h"

#include "base/atomic.h"
#include "base/opt_parser.h" 
#include "base/time_util.h"
namespace agora {
void SplitString(const std::string& s, std::set<std::string>& v, const std::string& c)
{
  std::string::size_type pos1, pos2;
  pos2 = s.find(c);
  pos1 = 0;
  while(std::string::npos != pos2)
  {
    v.insert(s.substr(pos1, pos2-pos1));
 
    pos1 = pos2 + c.size();
    pos2 = s.find(c, pos1);
  }
  if(pos1 != s.length())
    v.insert(s.substr(pos1));
}

AgoraSdk::AgoraSdk(agora::recording::IRecordingEngineEventHandler * handler) :
    m_level(agora::linuxsdk::AGORA_LOG_LEVEL_INFO)
    , m_mediaKeepTime(0)
    , m_lastAudioKeepTime(0)
    , m_lastVideoKeepTime(0)
    , m_subscribedVideoUids()
    , m_subscribedAudioUids()
    , m_keepLastFrame(false)
    , m_handler(handler)
{
  m_engine = NULL;
  m_stopped = false;
  m_storage_dir = "./";
  m_layoutMode = DEFAULT_LAYOUT;
  m_maxVertPreLayoutUid = -1;
  m_receivingAudio =false;
  m_receivingVideo =false;
  const char* env = getenv("KEEPMEDIATIME");
  if (env) {
    int time = (int)strtol(env, NULL, 0);
    m_mediaKeepTime = time >= 0 ? time : 0;
    printf("get media keep time from env with value : %d\n", m_mediaKeepTime);
  }
}

AgoraSdk::~AgoraSdk() {
  if (m_engine) {
    m_engine->release();
  }
}

bool AgoraSdk::stopped() const {
  return m_stopped;
}

bool AgoraSdk::release() {
  if (m_engine) {
    m_engine->release();
    m_engine = NULL;
  }

  return true;
}

uint32_t stringToUint(const std::string& str) {
  return static_cast<uint32_t>(strtol(str.c_str(), NULL, 10));
}

bool AgoraSdk::createChannel(const string &appid, const string &channelKey, const string &name,
    uint32_t uid, 
    agora::recording::RecordingConfig &config) 
{
  if (m_engine)
    return false;

  if ((m_engine = agora::recording::IRecordingEngine::createAgoraRecordingEngine(appid.c_str(), this->m_handler)) == NULL)
      return false;

  m_engine->setLogLevel(m_level);

  int ret = m_engine->joinChannel(channelKey.c_str(), name.c_str(), uid, config);
  if(linuxsdk::ERR_OK != ret)
      return false;
  if (!config.autoSubscribe) {
      if (config.subscribeVideoUids) {
        std::set<std::string> struids;
        SplitString(config.subscribeVideoUids, struids, ",");
        auto it = m_subscribedVideoUids.begin();
        std::transform(struids.begin(), struids.end(), std::inserter(m_subscribedVideoUids, it), stringToUint);
      }
      if (config.subscribeAudioUids) {
        std::set<std::string> struids;
        SplitString(config.subscribeAudioUids, struids, ",");
        auto it = m_subscribedAudioUids.begin();
        std::transform(struids.begin(), struids.end(), std::inserter(m_subscribedAudioUids, it), stringToUint);
      }
    }

  //m_engine->setUserBackground(30000, "test.jpg");

  m_config = config;
  return true;
}

bool AgoraSdk::createChannelWithUserAccount(const string &appid, const string &channelKey, const string &name,  const std::string& userAccount,
        agora::recording::RecordingConfig &config) {
  if (m_engine)
    return false;

  if ((m_engine = agora::recording::IRecordingEngine::createAgoraRecordingEngine(appid.c_str(), this->m_handler)) == NULL)
      return false;

  m_engine->setLogLevel(m_level);

  if(linuxsdk::ERR_OK != m_engine->joinChannelWithUserAccount(channelKey.c_str(), name.c_str(), userAccount.c_str(), config))
      return false;

  //m_engine->setUserBackground(30000, "test.jpg");
  if (!config.autoSubscribe) {
      if (config.subscribeVideoUids) {
        SplitString(config.subscribeVideoUids, m_subscribeVideoUserAccount, ",");
      }
      if (config.subscribeAudioUids) {
        SplitString(config.subscribeAudioUids, m_subscribeAudioUserAccount, ",");
      }
    }

  m_config = config;
  m_userAccount = userAccount;
  return true;
}


bool AgoraSdk::leaveChannel() {
  if (m_engine) {
    m_engine->leaveChannel();
    m_stopped = true;
  }

  return true;
}

bool AgoraSdk::stoppedOnError() {
  if (m_engine) {
    m_engine->stoppedOnError();
    m_stopped = true;
  }

  return true;
}

int AgoraSdk::startService() {
  if (m_engine) 
    return m_engine->startService();

  return 1;
}

int AgoraSdk::stopService() {
  if (m_engine) 
    return m_engine->stopService();

  return 1;
}

//Customize the layout of video under video mixing model
int AgoraSdk::setVideoMixLayout()
{
    //recording::RecordingConfig *pConfig = getConfigInfo();
    //size_t max_peers = pConfig->channelProfile == linuxsdk::CHANNEL_PROFILE_COMMUNICATION ? 7:17;
    if(!m_mixRes.m_videoMix) return 0;

    LAYOUT_MODE_TYPE layout_mode = m_layoutMode;
    uint32_t maxResolutionUid = 0;
    if (m_userAccount.length() > 0) {
      maxResolutionUid = getUidByUserAccount(m_maxVertPreLayoutUserAccount.c_str());
    } else {
      maxResolutionUid = m_maxVertPreLayoutUid;
    }

    std::vector<agora::linuxsdk::uid_t> subscribedUids;
    if (m_userAccount.length() > 0) {
      for (std::vector<agora::linuxsdk::uid_t>::iterator it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (m_config.autoSubscribe || (m_subscribedVideoUids.find(*it) != m_subscribedVideoUids.end())) {
          if (m_layoutMode == VERTICALPRESENTATION_LAYOUT) {
            char userAccount[256] = {0};
            uint32_t len = getUserAccountByUid(*it, userAccount, 256);
            if(len != 0 || 0 != maxResolutionUid) {
              subscribedUids.push_back(*it);
            }
          } else {
            subscribedUids.push_back(*it);
          }
        }
      }
    } else {
      for (std::vector<agora::linuxsdk::uid_t>::iterator it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (m_config.autoSubscribe || (m_subscribedVideoUids.find(*it) != m_subscribedVideoUids.end())) {
          subscribedUids.push_back(*it);
        }
      }
    }
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "setVideoMixLayout: user size: %d, keepLastFrame : %d, subscribed size : %d, permitted max_peers:%d, layout mode:%d, maxResolutionUid:%ld", m_peers.size(), m_keepLastFrame, subscribedUids.size(), max_peers, layout_mode, maxResolutionUid);

    agora::linuxsdk::VideoMixingLayout layout;
    layout.keepLastFrame = m_keepLastFrame ? 1 : 0;
    layout.canvasWidth = m_mixRes.m_width;
    layout.canvasHeight = m_mixRes.m_height;
    layout.backgroundColor = "#23b9dc";

    layout.regionCount = static_cast<uint32_t>(subscribedUids.size());

    if (!subscribedUids.empty()) {

        //CM_LOG_DIR(m_logdir.c_str(), INFO, "setVideoMixLayout: peers not empty");
        agora::linuxsdk::VideoMixingLayout::Region * regionList = new agora::linuxsdk::VideoMixingLayout::Region[subscribedUids.size()];
        if(layout_mode == BESTFIT_LAYOUT) {
            adjustBestFitVideoLayout(regionList, subscribedUids);
        }else if(layout_mode == VERTICALPRESENTATION_LAYOUT) {

            adjustVerticalPresentationLayout(maxResolutionUid, regionList, subscribedUids);
        }else {
            adjustDefaultVideoLayout(regionList, subscribedUids);
        }
        layout.regions = regionList;

    }
    else {
        layout.regions = NULL;
    }

    /*
    agora::linuxsdk::WatermarkConfig config[1];
    config[0].type = agora::linuxsdk::WATERMARK_TYPE_IMAGE;
    config[0].config.image.image_path = "/home/alex/test/test.png";
    config[0].config.image.offset_x = 20;
    config[0].config.image.offset_y = 20;
    config[0].config.image.wm_width = 200;
    config[0].config.image.wm_height = 300;

    config[1].type = agora::linuxsdk::WATERMARK_TYPE_TIMESTAMP;
    config[1].config.timestamp.font_size = 10;
    config[1].config.timestamp.offset_x = 20;
    config[1].config.timestamp.offset_y = 400;
    config[1].config.timestamp.wm_width = 200;
    config[1].config.timestamp.wm_height = 20;

    config[2].type = agora::linuxsdk::WATERMARK_TYPE_LITERA;
    config[2].config.litera.font_size = 10;
    config[2].config.litera.wm_litera = "test watermark";
    config[2].config.litera.offset_x = 20;
    config[2].config.litera.offset_y = 500;
    config[2].config.litera.wm_width = 200;
    config[2].config.litera.wm_height = 20;


    layout.wm_num = 1;
    layout.wm_configs = config;

    */
    int res = setVideoMixingLayout(layout);
    if(layout.regions)
        delete []layout.regions;

    return res;
}

void AgoraSdk::setLogLevel(agora::linuxsdk::agora_log_level level)
{
    m_level = level;
}

void AgoraSdk::setKeepLastFrame(bool keep) {
  m_keepLastFrame = keep;
}

int AgoraSdk::setVideoMixingLayout(const agora::linuxsdk::VideoMixingLayout &layout)
{
   int result = -agora::linuxsdk::ERR_INTERNAL_FAILED;
   if(m_engine)
      result = m_engine->setVideoMixingLayout(layout);
   return result;
}

int AgoraSdk::setUserBackground(agora::linuxsdk::uid_t uid, const char* image_path)
{
    int result = -agora::linuxsdk::ERR_INTERNAL_FAILED;
    if(m_engine)
        result = m_engine->setUserBackground(uid, image_path);
    return result;
}

const agora::recording::RecordingEngineProperties* AgoraSdk::getRecorderProperties(){
    return m_engine->getProperties();
}

int AgoraSdk::updateWatermarkConfigs(uint32_t wm_num, linuxsdk::WatermarkConfig* config) {
  if (m_engine) {
    return m_engine->updateWatermarkConfigs(wm_num, config);
  }
  return -1;
}

int AgoraSdk::updateSubscribeVideoUids(uint32_t *uids, uint32_t num) {
   if(m_engine) {
     return m_engine->updateSubscribeVideoUids(uids, num);
   }
   return -1;
}

int AgoraSdk::updateSubscribeAudioUids(uint32_t *uids, uint32_t num) {
  if (m_engine) {
    return m_engine->updateSubscribeAudioUids(uids, num);
  }
  return -1;
}

uint32_t AgoraSdk::getUidByUserAccount(const char *userAccount) {
  if (m_engine) {
    agora::linuxsdk::UserInfo info;
    m_engine->getUserInfoByUserAccount(userAccount, &info);
    return info.uid;
  }
  return 0;
}

uint32_t AgoraSdk::getUserAccountByUid(uint32_t uid, char* userAccountBuf, uint32_t buf_len) {
  if (m_engine) {
    agora::linuxsdk::UserInfo info;
    m_engine->getUserInfoByUid(uid, &info);
    uint32_t len = (uint32_t)strlen(info.userAccount);
    if (len > buf_len) {
      return len;
    } else {
      strncpy(userAccountBuf, info.userAccount, len);
      return len;
    }
  }
  return 0;
}

uint32_t AgoraSdk::now_s() const {
	struct timeval now = { 0, 0 };
	gettimeofday(&now, NULL);
	return (uint32_t)now.tv_sec;
}

void AgoraSdk::setMediaKeepTime(uint32_t time_ms) {
	m_mediaKeepTime = time_ms;
}

void AgoraSdk::adjustDefaultVideoLayout(agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {

    regionList[0].uid = subscribedUids[0];
    regionList[0].x = 0.f;
    regionList[0].y = 0.f;
    regionList[0].width = 1.f;
    regionList[0].height = 1.f;
    regionList[0].alpha = 1.f;
    regionList[0].renderMode = 0;

    //CM_LOG_DIR(m_logdir.c_str(), INFO, "region 0 uid: %u, x: %f, y: %f, width: %f, height: %f, alpha: %f", regionList[0].uid, regionList[0].x, regionList[0].y, regionList[0].width, regionList[0].height, regionList[0].alpha);


    float canvasWidth = static_cast<float>(m_mixRes.m_width);
    float canvasHeight = static_cast<float>(m_mixRes.m_height);

    float viewWidth = 0.235f;
    float viewHEdge = 0.012f;
    float viewHeight = viewWidth * (canvasWidth/canvasHeight);
    float viewVEdge = viewHEdge * (canvasWidth/canvasHeight);

    for (size_t i=1; i<subscribedUids.size(); i++) {

        regionList[i].uid = subscribedUids[i];

        float xIndex = static_cast<float>((i-1) % 4);
        float yIndex = static_cast<float>((i-1) / 4);
        regionList[i].x = xIndex * (viewWidth + viewHEdge) + viewHEdge;
        regionList[i].y = 1 - (yIndex + 1) * (viewHeight + viewVEdge);
        regionList[i].width = viewWidth;
        regionList[i].height = viewHeight;
        regionList[i].alpha = static_cast<double>(i + 1);
        regionList[i].renderMode = 0;
    }
}

void AgoraSdk::setMaxResolutionUid(int number, unsigned int maxResolutionUid, agora::linuxsdk::VideoMixingLayout::Region * regionList, double weight_ratio) {
    regionList[number].uid = maxResolutionUid;
    regionList[number].x = 0.f;
    regionList[number].y = 0.f;
    regionList[number].width = 1.f * weight_ratio;
    regionList[number].height = 1.f;
    regionList[number].alpha = 1.f;
    regionList[number].renderMode = 1;
}
void AgoraSdk::changeToVideo7Layout(unsigned int maxResolutionUid, agora::linuxsdk::VideoMixingLayout::Region * regionList, std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //CM_LOG_DIR(m_logdir.c_str(), INFO,"changeToVideo7Layout");
    adjustVideo7Layout(maxResolutionUid, regionList, subscribedUids);
}
void AgoraSdk::changeToVideo9Layout(unsigned int maxResolutionUid, agora::linuxsdk::VideoMixingLayout::Region * regionList, std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //CM_LOG_DIR(m_logdir.c_str(), INFO,"changeToVideo9Layout");
    adjustVideo9Layout(maxResolutionUid, regionList, subscribedUids);
}
void AgoraSdk::changeToVideo17Layout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //CM_LOG_DIR(m_logdir.c_str(), INFO,"changeToVideo17Layout");
    adjustVideo17Layout(maxResolutionUid, regionList, subscribedUids);
}

void AgoraSdk::adjustVideo5Layout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    bool flag = false;
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo5Layou tegion 0 uid: %u, x: %f, y: %f, width: %f, height: %f, alpha: %f", regionList[0].uid, regionList[0].x, regionList[0].y, regionList[0].width, regionList[0].height, regionList[0].alpha);

    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);

    //float viewWidth = 0.235f;
    //float viewHEdge = 0.012f;
    int number = 0;

    size_t i=0;
    for (; i<subscribedUids.size(); i++) {
        if(maxResolutionUid == subscribedUids[i]){
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo5Layout equal with configured user uid:%u", maxResolutionUid);
            flag = true;
            setMaxResolutionUid(number,  maxResolutionUid, regionList,0.8);
            number++;
            continue;
        }
        regionList[number].uid = subscribedUids[i];
        //float xIndex = ;
        float yIndex = flag?static_cast<float>(number-1 % 4):static_cast<float>(number % 4);
        regionList[number].x = 1.f * 0.8;
        regionList[number].y = (0.25) * yIndex;
        regionList[number].width = 1.f*(1-0.8);
        regionList[number].height = 1.f * (0.25);
        regionList[number].alpha = 1;//static_cast<double>(number);
        regionList[number].renderMode = 0;
        number++;
        if(i == 4 && !flag){
            changeToVideo7Layout(maxResolutionUid, regionList, subscribedUids);
        }
    }

}
void AgoraSdk::adjustVideo7Layout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t> &subscribedUids) {
    bool flag = false;
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo7Layou tegion 0 uid: %u, x: %f, y: %f, width: %f, height: %f, alpha: %f", regionList[0].uid, regionList[0].x, regionList[0].y, regionList[0].width, regionList[0].height, regionList[0].alpha);

    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);

    //float viewWidth = 0.235f;
    //float viewHEdge = 0.012f;
    int number = 0;

    size_t i=0;
    for (; i<subscribedUids.size(); i++) {
        if(maxResolutionUid == subscribedUids[i]){
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo7Layout equal with configured user uid:%u", maxResolutionUid);
            flag = true;
            setMaxResolutionUid(number,  maxResolutionUid, regionList,6.f/7);
            number++;
            continue;
        }
        regionList[number].uid = subscribedUids[i];
        float yIndex = flag?static_cast<float>(number-1 % 6):static_cast<float>(number % 6);
        regionList[number].x = 6.f/7;
        regionList[number].y = (1.f/6) * yIndex;
        regionList[number].width = (1.f/7);
        regionList[number].height = (1.f/6);
        regionList[number].alpha = 1;//static_cast<double>(number);
        regionList[number].renderMode = 0;
        number++;
        if(i == 6 && !flag){
            changeToVideo9Layout(maxResolutionUid, regionList, subscribedUids);
        }
    }

}
void AgoraSdk::adjustVideo9Layout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    bool flag = false;
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo9Layou tegion 0 uid: %u, x: %f, y: %f, width: %f, height: %f, alpha: %f", regionList[0].uid, regionList[0].x, regionList[0].y, regionList[0].width, regionList[0].height, regionList[0].alpha);

    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);

    //float viewWidth = 0.235f;
    //float viewHEdge = 0.012f;
    int number = 0;

    size_t i=0;
    for (; i<subscribedUids.size(); i++) {
        if(maxResolutionUid == subscribedUids[i]){
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo9Layout equal with configured user uid:%u", maxResolutionUid);
            flag = true;
            setMaxResolutionUid(number,  maxResolutionUid, regionList,9.f/5);
            number++;
            continue;
        }
        regionList[number].uid = subscribedUids[i];
        float yIndex = flag?static_cast<float>(number-1 % 8):static_cast<float>(number % 8);
        regionList[number].x = 8.f/9;
        regionList[number].y = (1.f/8) * yIndex;
        regionList[number].width = 1.f/9 ;
        regionList[number].height = 1.f/8;
        regionList[number].alpha = 1;//static_cast<double>(number);
        regionList[number].renderMode = 0;
        number++;
        if(i == 8 && !flag){
            changeToVideo17Layout(maxResolutionUid, regionList, subscribedUids);
        }
    }
}

void AgoraSdk::adjustVideo17Layout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    bool flag = false;
    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);

    //float viewWidth = 0.235f;
    //float viewHEdge = 0.012f;
    int number = 0;
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo17Layoutenter subscribedUids size is:%d, maxResolutionUid:%ld",subscribedUids.size(), maxResolutionUid);
    for (size_t i=0; i<subscribedUids.size(); i++) {
        if(maxResolutionUid == subscribedUids[i]){
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustVideo17Layoutenter equal with maxResolutionUid:%ld", maxResolutionUid);
            flag = true;
            setMaxResolutionUid(number,  maxResolutionUid, regionList,0.8);
            number++;
            continue;
        }
        if(!flag && i == 16) {
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "Not the configured uid, and small regions is sixteen, so ignore this user:%d",subscribedUids[i]);
            break;
        }
        regionList[number].uid = subscribedUids[i];
        //float xIndex = 0.833f;
        float yIndex = flag?static_cast<float>((number-1) % 8):static_cast<float>(number % 8);
        regionList[number].x = ((flag && i>8) || (!flag && i >=8)) ? (9.f/10):(8.f/10);
        regionList[number].y = (1.f/8) * yIndex;
        regionList[number].width =  1.f/10 ;
        regionList[number].height = 1.f/8;
        regionList[number].alpha = 1; //static_cast<double>(number);
        regionList[number].renderMode = 0;
        number++;
    }
}

void AgoraSdk::adjustVerticalPresentationLayout(unsigned int maxResolutionUid,
    agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //CM_LOG_DIR(m_logdir.c_str(), INFO, "begin adjust vertical presentation layout,peers size:%d, maxResolutionUid:%ld",subscribedUids.size(), maxResolutionUid);
    if(subscribedUids.size() <= 5) {
        adjustVideo5Layout(maxResolutionUid, regionList, subscribedUids);
    }else if(subscribedUids.size() <= 7) {
        adjustVideo7Layout(maxResolutionUid, regionList, subscribedUids);
    }else if(subscribedUids.size() <= 9) {
        adjustVideo9Layout(maxResolutionUid, regionList, subscribedUids);
    }else {
        adjustVideo17Layout(maxResolutionUid, regionList, subscribedUids);
    }
}

void AgoraSdk::adjustBestFitLayout_2(agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);
    //float viewWidth = 0.235f;
    //float viewHEdge = 0.012f;
    size_t peersCount = subscribedUids.size();
    for (size_t i=0; i < peersCount; i++) {
        regionList[i].uid = subscribedUids[i];
        regionList[i].x = ((i+1)%2)?0:0.5;
        regionList[i].y =  0.f;
        regionList[i].width = 0.5f;
        regionList[i].height = 1.f;
        regionList[i].alpha = static_cast<double>(i+1);
        regionList[i].renderMode = 0;
    }
}
void AgoraSdk::adjustBestFitLayout_Square(agora::linuxsdk::VideoMixingLayout::Region * regionList, int nSquare, std::vector<agora::linuxsdk::uid_t> &subscribedUids) {
    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);
    float viewWidth = static_cast<float>(1.f * 1.0/nSquare);
    float viewHEdge = static_cast<float>(1.f * 1.0/nSquare);
    size_t peersCount = subscribedUids.size();
    for (size_t i=0; i < peersCount; i++) {
        float xIndex =static_cast<float>(i%nSquare);
        float yIndex = static_cast<float>(i/nSquare);
        regionList[i].uid = subscribedUids[i];
        regionList[i].x = 1.f * 1.0/nSquare * xIndex;
        regionList[i].y = 1.f * 1.0/nSquare * yIndex;
        regionList[i].width = viewWidth;
        regionList[i].height = viewHEdge;
        regionList[i].alpha = static_cast<double>(i+1);
        regionList[i].renderMode = 0;
    }
}
void AgoraSdk::adjustBestFitLayout_17(agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    //float canvasWidth = static_cast<float>(m_mixRes.m_width);
    //float canvasHeight = static_cast<float>(m_mixRes.m_height);
    int n = 5;
    float viewWidth = static_cast<float>(1.f * 1.0/n);
    float viewHEdge = static_cast<float>(1.f * 1.0/n);
    //float totalWidth = static_cast<float>(1.f - viewWidth);
    size_t peersCount = subscribedUids.size();
    for (size_t i=0; i < peersCount; i++) {
        float xIndex = static_cast<float>(i%(n-1));
        float yIndex = static_cast<float>(i/(n-1));
        regionList[i].uid = subscribedUids[i];
        regionList[i].width = viewWidth;
        regionList[i].height = viewHEdge;
        regionList[i].alpha = static_cast<double>(i+1);
        regionList[i].renderMode = 0;
        if(i == 16) {
            regionList[i].x = (1-viewWidth)*(1.f/2) * 1.f;
            //CM_LOG_DIR(m_logdir.c_str(), INFO, "special layout for 17 x is:",regionList[i].x);
        }else {
            regionList[i].x = 0.5f * viewWidth +  viewWidth * xIndex;
        }
        regionList[i].y =  (1.0/n) * yIndex;
    }
}

void AgoraSdk::adjustBestFitVideoLayout(agora::linuxsdk::VideoMixingLayout::Region * regionList,
    std::vector<agora::linuxsdk::uid_t>& subscribedUids) {
    if(subscribedUids.size() == 1) {
        adjustBestFitLayout_Square(regionList,1, subscribedUids);
    }else if(subscribedUids.size() == 2) {
        adjustBestFitLayout_2(regionList, subscribedUids);
    }else if( 2 < subscribedUids.size() && subscribedUids.size() <=4) {
        adjustBestFitLayout_Square(regionList,2, subscribedUids);
    }else if(5<=subscribedUids.size() && subscribedUids.size() <=9) {
        adjustBestFitLayout_Square(regionList,3, subscribedUids);
    }else if(10<=subscribedUids.size() && subscribedUids.size() <=16) {
        adjustBestFitLayout_Square(regionList,4, subscribedUids);
    }else if(subscribedUids.size() ==17) {
        adjustBestFitLayout_17(regionList, subscribedUids);
    }else {
        //CM_LOG_DIR(m_logdir.c_str(), INFO, "adjustBestFitVideoLayout is more than 17 users");
    }
}
}

