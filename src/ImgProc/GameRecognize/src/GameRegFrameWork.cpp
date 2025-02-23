/*
  * Tencent is pleased to support the open source community by making GameAISDK available.

  * This source code file is licensed under the GNU General Public License Version 3.
  * For full details, please refer to the file "LICENSE.txt" which is provided as part of this source code package.

  * Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
*/

#include "GameRecognize/src/GameRegFrameWork.h"

extern ThreadSafeQueue<CTaskResult> g_oTaskResultQueue;
extern std::string                  g_strBaseDir;
extern std::string                  g_userCfgPath;
extern int                          g_nMaxResultSize;

CGameRegFrameWork::CGameRegFrameWork() {
    m_bExited = false;
    m_bTestFlag = false;
    m_nFrameRate = -1;
    m_bMultiResolution = true;
    m_bConnectAgent = false;
    m_eTestMode = SDK_TEST;
}

CGameRegFrameWork::~CGameRegFrameWork() {
}


int CGameRegFrameWork::Initialize(char* pszSysCfgPath, char* pszUserCfgFilePath) {
    m_bExited = false;
    m_bTestFlag = false;
    m_bConnectAgent = false;

    // 读取配置文件，同时获取线程数
    LOGI("load config file");
    int nThreadNum;
    if (-1 == LoadConfg(&nThreadNum, pszSysCfgPath, pszUserCfgFilePath)) {
        LOGE("load cfg failed");
        return -1;
    }

    // 创建内存池
    if (0 != CGameRegMemPool::getInstance()->CreateMemPool(MEM_POOL_SIZE)) {
        LOGE("memPool initial failed");
        return -1;
    }

    // 如果不是视频测试模式，则需要初始化网络
    if (m_eTestMode != VIDEO_TEST) {
        LOGI("initialize network manager");
        if (0 != m_oNetWorkManager.Initialize(pszSysCfgPath)) {
            LOGE("NetWorkManager initial failed");
            return -1;
        }
    }

    // 线程池初始化
    if (!m_oThreadPoolMgr.Initialize(nThreadNum)) {
        LOGE("ThreadPool initial failed");
        return -1;
    }

    // taskManager初始化
    bool bSendSDKTool = false;
    if (m_eTestMode == SDKTOOL_TEST) {
        bSendSDKTool = true;
    }
    LOGI("send sdk tool:%s", bSendSDKTool ? "true" : "false");

    if (!m_oTaskManager.Initialize(&m_oThreadPoolMgr, bSendSDKTool, m_bMultiResolution,
        pszSysCfgPath)) {
        LOGE("TaskManager initial failed");
        return -1;
    }

    return 0;
}

bool CGameRegFrameWork::CreateLog(const char *strProgName, const char *strLevel) {
    // 解析日志级别
    std::string strLogLevel(strLevel);

    if (strLogLevel == "DEBUG") {
        g_logLevel = FACE_DEBUG;
    } else if (strLogLevel == "INFO") {
        g_logLevel = FACE_INFO;
    } else if (strLogLevel == "WARN") {
        g_logLevel = FACE_WARNING;
    } else if (strLogLevel == "ERROR") {
        g_logLevel = FACE_ERROR;
    } else {
        LOGW("log level is wrong");
    }

    // 创建日志，并初始化
    CLog *pLog = CLog::getInstance();
    int  nResult = pLog->init(strProgName, 7);

    if (nResult) {
        LOGE("Create log file failed");
        return false;
    }

    return true;
}

int CGameRegFrameWork::Run() {
    switch (m_eTestMode) {
        // 视频测试
    case VIDEO_TEST:
    {
        if (-1 == RunForVideo()) {
            LOGE("run for video failed");
            return -1;
        }

        break;
    }

    // AI_SDK模式
    case SDK_TEST:
    {
        if (-1 == RunForSDK()) {
            LOGE("run for SDK failed");
            return -1;
        }

        break;
    }

    // 与SDKTool联调
    case SDKTOOL_TEST:
    {
        if (-1 == RunForSDKTool()) {
            LOGE("run for SDKTool failed");
            return -1;
        }

        break;
    }

    default:
    {
        LOGE("wrong type of test mode: %d", m_eTestMode);
        return -1;
    }
    }

    return 0;
}

int CGameRegFrameWork::RunForSDK() {
    // 定义各模块需要交互的数据结构

    tagFrameResult            stFrameResult;
    std::vector<tagTask>      oVecRst;
    std::vector<CTaskMessage> oVecTaskMsg;
    ESendTaskReportToMC       eSendTaskReportToMC = SEND_NO;

    while (!m_bExited) {
        tagSrcImgInfo stSrcImageInfo;
        // 时间更新
        CGameTime::getInstance()->Update();

        // 网络模块更新
        int netRet = m_oNetWorkManager.Update(&oVecTaskMsg, &stSrcImageInfo,
            stFrameResult, &eSendTaskReportToMC);
        manageTask(netRet, stFrameResult, oVecTaskMsg, stSrcImageInfo, &eSendTaskReportToMC);
    }

    releaseResource(stFrameResult, oVecTaskMsg);

    return 0;
}

void CGameRegFrameWork::manageTask(int netRet, tagFrameResult &stFrameResult, std::vector<CTaskMessage> oVecTaskMsg,
    tagSrcImgInfo &stSrcImageInfo, ESendTaskReportToMC *eSendTaskReportToMC) {
    stFrameResult.Release();

    // 任务管理模块更新
    int updateRet = UpdateTask(stSrcImageInfo, oVecTaskMsg, &stFrameResult, eSendTaskReportToMC);
    for (std::size_t idx = 0; idx < oVecTaskMsg.size(); ++idx) {
        oVecTaskMsg[idx].Release();
    }

    // 都没有更新数据, 则进行一定时间的睡眠
    if (netRet != 1 && updateRet != 1) {
        TqcOsSleep(LOOP_RUN_SLEEP);
    }
}

void CGameRegFrameWork::releaseResource(tagFrameResult &stFrameResult, std::vector<CTaskMessage> oVecTaskMsg) {
    stFrameResult.Release();

    for (std::size_t idx = 0; idx < oVecTaskMsg.size(); ++idx) {
        oVecTaskMsg[idx].Release();
    }
}

int CGameRegFrameWork::UpdateTask(tagSrcImgInfo &stSrcImageInfo,
    std::vector<CTaskMessage> oVecTaskMsg, tagFrameResult *stFrameResult,
    ESendTaskReportToMC *eSendTaskReportToMC) {
    std::vector<tagTask>      oVecRst;

    if (stSrcImageInfo.oSrcImage.cols > 0 && stSrcImageInfo.oSrcImage.rows > 0) {
        LOGI("recv image from sdktool width %d, height %d",
            stSrcImageInfo.oSrcImage.cols, stSrcImageInfo.oSrcImage.rows);
    }

    // 任务管理模块更新
    int nRet = m_oTaskManager.Update(oVecTaskMsg, stSrcImageInfo, &oVecRst, stFrameResult,
        eSendTaskReportToMC);

    // 线程池更新
    m_oThreadPoolMgr.Update(oVecRst);
    return nRet;
}

int CGameRegFrameWork::RunForSDKTool() {
    // 定义各模块需要交互的数据结构
    tagFrameResult            stFrameResult;
    std::vector<tagTask>      oVecRst;
    std::vector<CTaskMessage> oVecTaskMsg;
    ESendTaskReportToMC       eSendTaskReportToMC = SEND_NO;

    while (!m_bExited) {
        tagSrcImgInfo stSrcImageInfo;
        // 时间更新
        CGameTime::getInstance()->Update();

        // 网络模块更新
        eSendTaskReportToMC = SEND_NO;
        int netRet = m_oNetWorkManager.UpdateForSDKTool(&oVecTaskMsg, &stSrcImageInfo,
            stFrameResult);
        manageTask(netRet, stFrameResult, oVecTaskMsg, stSrcImageInfo, &eSendTaskReportToMC);
    }

    releaseResource(stFrameResult, oVecTaskMsg);

    return 0;
}

int CGameRegFrameWork::RunForVideo() {
    // 定义各模块需要交互的数据结构
    tagFrameResult            stFrameResult;
    std::vector<tagTask>      oVecRst;
    std::vector<CTaskMessage> oVecTaskMsg;
    ESendTaskReportToMC       eSendTaskReportToMC = SEND_NO;

    // 初始化消息，因为不和SDK一起跑，所以模拟Agent发送任务初始化的消息
    CTaskMessage oTaskMessage;

    oTaskMessage.SetMsgType(MSG_RECV_CONF_TASK);
    tagCmdMsg      *pCmdMsg = oTaskMessage.GetInstance(MSG_RECV_CONF_TASK);
    tagConfTaskMsg *pConfTaskMsg = dynamic_cast<tagConfTaskMsg*>(pCmdMsg);
    if (NULL == pConfTaskMsg) {
        LOGE("pConfTaskMsg is NULL");
        return -1;
    }

    pConfTaskMsg->strVecConfName.push_back("task.json");
    pConfTaskMsg->strVecConfName.push_back(g_userCfgPath + GAMEREG_REFER_CFG);
    oVecTaskMsg.push_back(oTaskMessage);

    // 读取task配置文件
    if (-1 == LoadTaskCfg(&oVecTaskMsg, GAMEREG_TASK_CFG)) {
        LOGE("load task.json failed");
        return -1;
    }

    // 读取视频文件
    cv::VideoCapture cap;
    cap.open(m_strTestVideo);

    if (!cap.isOpened()) {
        LOGE("can not open %s", m_strTestVideo.c_str());
        return -1;
    }

    int nFrameSeq = 1;

    int fFrameRateMs = 1000 / m_nFrameRate;

    while (!m_bExited) {
        int           nMacroBeginSecond = TqcOsGetMicroSeconds();
        tagSrcImgInfo stSrcImageInfo;
        // 时间更新
        CGameTime::getInstance()->Update();

        cap.read(stSrcImageInfo.oSrcImage);
        if (stSrcImageInfo.oSrcImage.empty()) {
            LOGI("video is over");
            break;
        }

        LOGI("recv image from video width %d, height %d",
            stSrcImageInfo.oSrcImage.cols, stSrcImageInfo.oSrcImage.rows);
        stSrcImageInfo.uFrameSeq = nFrameSeq++;

        int updateRet = UpdateTask(stSrcImageInfo, oVecTaskMsg, &stFrameResult,
            &eSendTaskReportToMC);

        for (size_t idx = 0; idx < oVecTaskMsg.size(); ++idx) {
            oVecTaskMsg[idx].Release();
        }

        if (updateRet != 1) {
            TqcOsSleep(LOOP_RUN_SLEEP);
        }

        stFrameResult.Release();
        oVecTaskMsg.clear();

        // 按照设定的帧率sleep一段时间
        int nMacroEndSecond = TqcOsGetMicroSeconds();
        int fTimeMs = (nMacroEndSecond - nMacroBeginSecond) / 1000;
        if (fTimeMs < fFrameRateMs) {
            TqcOsSleep(fFrameRateMs - fTimeMs);
        }
    }

    // 释放资源
    cap.release();
    stFrameResult.Release();

    for (size_t idx = 0; idx < oVecTaskMsg.size(); ++idx) {
        oVecTaskMsg[idx].Release();
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskCfg(std::vector<CTaskMessage> *oVecTaskMsg, std::string strTaskCfg) {
    Json::Reader reader;
    Json::Value  root;

    // 获取task.json的绝对路径
    strTaskCfg = g_userCfgPath + strTaskCfg;
    std::ifstream iFile(strTaskCfg);
    if (!iFile.is_open()) {
        LOGE("can not open : %s", strTaskCfg.c_str());
        return -1;
    }

    if (!reader.parse(iFile, root)) {
        LOGE("can not read json content: %s", strTaskCfg.c_str());
        return -1;
    }

    // ================================================================
    // 以下为task.json文件的解析，可根据其结构来阅读代码
    // ================================================================
    CTaskMessage oTaskMessage;
    oTaskMessage.SetMsgType(MSG_RECV_GROUP_ID);
    tagCmdMsg   *pstCmdMsg = oTaskMessage.GetInstance(MSG_RECV_GROUP_ID);
    tagAgentMsg *pstAgentMsg = dynamic_cast<tagAgentMsg*>(pstCmdMsg);
    if (NULL == pstAgentMsg) {
        LOGE("pstAgentMsg is NULL");
        return -1;
    }

    int nGroupSize = root["alltask"].size();

    for (int nGroupIdx = 0; nGroupIdx < nGroupSize; ++nGroupIdx) {
        int         nGroupID = root["alltask"][nGroupIdx]["groupID"].asInt();
        Json::Value oTaskList = root["alltask"][nGroupIdx]["task"];
        int         nTaskSize = oTaskList.size();
        pstAgentMsg->uGroupID = nGroupID;

        // 依次加载配置项
        //xdien Khoi dong dang ky task tai day
        for (int nIdx = 0; nIdx < nTaskSize; ++nIdx) {
            Json::Value oTask = oTaskList[nIdx];
            int         nTaskID = oTask["taskID"].asInt();
            std::string strType = oTask["type"].asString();
            pstAgentMsg->mapTaskParams[nTaskID] = CTaskParam();
            pstAgentMsg->mapTaskParams[nTaskID].SetTaskID(nTaskID);
            // pstAgentMsg->mapTaskParams[nTaskID].SetSkipFrame(oTask["skipFrame"].asInt());
            // 读取fix object类型的配置
            if (strType == "fix object") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_FIXOBJREG);
                IRegParam *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_FIXOBJREG);
                CFixObjRegParam *pFixObjRegParam = dynamic_cast<CFixObjRegParam*>(pRegParam);

                if (-1 == LoadTaskFixObj(oTask["elements"], pFixObjRegParam)) {
                    LOGE("load fix obj task failed");
                    return -1;
                }
            } else if (strType == "pixel") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_PIXREG);
                IRegParam    *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_PIXREG);
                CPixRegParam *pPixRegParam = dynamic_cast<CPixRegParam*>(pRegParam);

                if (-1 == LoadTaskPix(oTask["elements"], pPixRegParam)) {
                    LOGE("load pix obj task failed");
                    return -1;
                }
            } else if (strType == "stuck") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_STUCKREG);
                IRegParam      *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_STUCKREG);
                CStuckRegParam *pStuckRegParam = dynamic_cast<CStuckRegParam*>(pRegParam);

                if (-1 == LoadTaskStuck(oTask["elements"], pStuckRegParam)) {
                    LOGE("load stuck task failed");
                    return -1;
                }
            } else if (strType == "deform object") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_DEFORMOBJ);
                IRegParam          *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_DEFORMOBJ);
                CDeformObjRegParam *pDeformObjRegParam
                    = dynamic_cast<CDeformObjRegParam*>(pRegParam);

                if (-1 == LoadTaskDeform(oTask["elements"], pDeformObjRegParam)) {
                    LOGE("load deform task failed");
                    return -1;
                }
            } else if (strType == "number") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_NUMBER);
                IRegParam    *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_NUMBER);
                CNumRegParam *pNumRegParam = dynamic_cast<CNumRegParam*>(pRegParam);

                if (-1 == LoadTaskNumber(oTask["elements"], pNumRegParam)) {
                    LOGE("load number task failed");
                    return -1;
                }
            } else if (strType == "fix blood") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_FIXBLOOD);
                IRegParam *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_FIXBLOOD);
                CFixBloodRegParam *pFixBloodRegParam = dynamic_cast<CFixBloodRegParam*>(pRegParam);

                if (-1 == LoadTaskFixBlood(oTask["elements"], pFixBloodRegParam)) {
                    LOGE("load number task failed");
                    return -1;
                }
            } else if (strType == "king glory blood") {
                pstAgentMsg->mapTaskParams[nTaskID].SetType(TYPE_KINGGLORYBLOOD);
                IRegParam          *pRegParam
                    = pstAgentMsg->mapTaskParams[nTaskID].GetInstance(TYPE_KINGGLORYBLOOD);
                CDeformObjRegParam *pDeformObjRegParam
                    = dynamic_cast<CDeformObjRegParam*>(pRegParam);

                if (-1 == LoadTaskDeform(oTask["elements"], pDeformObjRegParam)) {
                    LOGE("load deform king glory blood task failed");
                    return -1;
                }
            } else {
                LOGE("wrong type: %s", strType.c_str());
                return -1;
            }
        }
    }

    oVecTaskMsg->push_back(oTaskMessage);

    return 0;
}

int CGameRegFrameWork::LoadTaskFixObj(Json::Value oTaskFixObjElements,
    CFixObjRegParam *pFixObjRegParam) {
    // 检查输入参数的合法性
    if (pFixObjRegParam == nullptr) {
        LOGE("pFixObjRegParam is NULL");
        return -1;
    }

    int nSize = oTaskFixObjElements.size();

    // 遍历每一个element分别获取其参数，并保存在pFixObjRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagFixObjRegElement stFixObjRegElement;
        Json::Value         oFixObjElement = oTaskFixObjElements[nIdx];
        if (-1 == LoadRect(oFixObjElement["ROI"], &stFixObjRegElement.oROI)) {
            LOGE("load rect failed when load fix obj");
            return -1;
        }

        stFixObjRegElement.strAlgorithm = oFixObjElement["algorithm"].asString();
        stFixObjRegElement.fMinScale = oFixObjElement["minScale"].asFloat();
        stFixObjRegElement.fMaxScale = oFixObjElement["maxScale"].asFloat();
        stFixObjRegElement.nScaleLevel = oFixObjElement["scaleLevel"].asInt();

        if (-1 == LoadTemplates(oFixObjElement["templates"], &stFixObjRegElement.oVecTmpls)) {
            LOGE("load templates failed when load fix obj");
            return -1;
        }

        pFixObjRegParam->m_oVecElements.push_back(stFixObjRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskPix(Json::Value oTaskPixElements, CPixRegParam *pPixRegParam) {
    // 检查输入参数的合法性
    if (pPixRegParam == nullptr) {
        LOGE("pPixRegParam is NULL");
        return -1;
    }

    int nSize = oTaskPixElements.size();

    // 遍历每一个element分别获取其参数，并保存在pPixRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagPixRegElement stPixRegElement;
        Json::Value      oPixElement = oTaskPixElements[nIdx];
        if (-1 == LoadRect(oPixElement["ROI"], &stPixRegElement.oROI)) {
            LOGE("load rect failed when load pix");
            return -1;
        }

        stPixRegElement.strCondition = oPixElement["condition"].asString();
        stPixRegElement.nFilterSize = oPixElement["filterSize"].asInt();
        stPixRegElement.nMaxPointNum = oPixElement["maxPointNum"].asInt();

        pPixRegParam->m_oVecElements.push_back(stPixRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskNumber(Json::Value oTaskNumberElements, CNumRegParam *pNumRegParam) {
    // 检查输入参数的合法性
    if (pNumRegParam == nullptr) {
        LOGE("pNumRegParam is NULL");
        return -1;
    }

    int nSize = oTaskNumberElements.size();

    // 遍历每一个element分别获取其参数，并保存在pNumRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagNumRegElement stNumRegElement;
        Json::Value      oNumRegElement = oTaskNumberElements[nIdx];
        if (-1 == LoadRect(oNumRegElement["ROI"], &stNumRegElement.oROI)) {
            LOGE("load rect failed when load number reg");
            return -1;
        }

        stNumRegElement.oAlgorithm = oNumRegElement["algorithm"].asString();
        stNumRegElement.fMinScale = oNumRegElement["minScale"].asFloat();
        stNumRegElement.fMaxScale = oNumRegElement["maxScale"].asFloat();
        stNumRegElement.nScaleLevel = oNumRegElement["scaleLevel"].asInt();

        if (-1 == LoadTemplates(oNumRegElement["templates"], &stNumRegElement.oVecTmpls)) {
            LOGE("load templates failed when load number reg");
            return -1;
        }

        pNumRegParam->m_oVecElements.push_back(stNumRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskStuck(Json::Value oTaskStuckElements,
    CStuckRegParam *pStuckRegParam) {
    // 检查输入参数的合法性
    if (pStuckRegParam == nullptr) {
        LOGE("pStuckRegParam is NULL");
        return -1;
    }

    int nSize = oTaskStuckElements.size();

    // 遍历每一个element分别获取其参数，并保存在pStuckRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagStuckRegElement stStuckRegElement;
        Json::Value        oStuckElement = oTaskStuckElements[nIdx];
        if (-1 == LoadRect(oStuckElement["ROI"], &stStuckRegElement.oROI)) {
            LOGE("load rect failed when load stuck reg");

            return -1;
        }

        stStuckRegElement.fIntervalTime = oStuckElement["intervalTime"].asFloat();
        stStuckRegElement.fThreshold = oStuckElement["threshold"].asFloat();

        pStuckRegParam->m_oVecElements.push_back(stStuckRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskDeform(Json::Value oTaskDeformElements,
    CDeformObjRegParam *pDeformRegParam) {
    // 检查输入参数的合法性
    if (pDeformRegParam == nullptr) {
        LOGE("pDeformRegParam is NULL");
        return -1;
    }

    int nSize = oTaskDeformElements.size();

    // 遍历每一个element分别获取其参数，并保存在pStuckRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagDeformObjRegElement stDeformObjRegElement;
        Json::Value            oDeformElement = oTaskDeformElements[nIdx];
        if (-1 == LoadRect(oDeformElement["ROI"], &stDeformObjRegElement.oROI)) {
            LOGE("load rect failed when load deform reg");
            return -1;
        }

        stDeformObjRegElement.strNamePath = g_userCfgPath + oDeformElement["namePath"].asString();
        stDeformObjRegElement.strWeightPath = g_userCfgPath +
            oDeformElement["weightPath"].asString();
        stDeformObjRegElement.strCfgPath = g_userCfgPath + oDeformElement["cfgPath"].asString();
        stDeformObjRegElement.fThreshold = oDeformElement["threshold"].asFloat();

        pDeformRegParam->m_oVecElements.push_back(stDeformObjRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadTaskFixBlood(Json::Value oTaskFixBloodElements,
    CFixBloodRegParam *pFixBloodRegParam) {
    // 检查输入参数的合法性
    if (pFixBloodRegParam == nullptr) {
        LOGE("pFixBloodRegParam is NULL");
        return -1;
    }

    int nSize = oTaskFixBloodElements.size();

    // 遍历每一个element分别获取其参数，并保存在pStuckRegParam->m_oVecElements中
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        tagFixBloodRegParam stFixBloodRegElement;
        Json::Value         oFixBloodElement = oTaskFixBloodElements[nIdx];

        if (-1 == LoadRect(oFixBloodElement["ROI"], &stFixBloodRegElement.oROI)) {
            LOGE("load rect failed when load FixBlood reg");
            return -1;
        }

        stFixBloodRegElement.nFilterSize = oFixBloodElement["condition"].asInt();
        stFixBloodRegElement.strCondition = oFixBloodElement["bloodLength"].asString();
        stFixBloodRegElement.nBloodLength = oFixBloodElement["filterSize"].asInt();
        stFixBloodRegElement.nMaxPointNum = oFixBloodElement["maxPointNum"].asInt();

        pFixBloodRegParam->m_oVecElements.push_back(stFixBloodRegElement);
    }

    return 0;
}

int CGameRegFrameWork::LoadRect(Json::Value oJsonRect, cv::Rect *oRect) {
    oRect->x = oJsonRect["x"].asInt();
    oRect->y = oJsonRect["y"].asInt();
    oRect->width = oJsonRect["w"].asInt();
    oRect->height = oJsonRect["h"].asInt();
    return 0;
}

int CGameRegFrameWork::LoadTemplates(Json::Value oJsonTemplates,
    std::vector<tagTmpl> *oVecTemplates) {
    int nSize = oJsonTemplates.size();

    // 获取模板的相关参数
    for (int nIdx = 0; nIdx < nSize; ++nIdx) {
        Json::Value oJsonTemplate = oJsonTemplates[nIdx];
        tagTmpl     stTmpl;
        if (-1 == LoadRect(oJsonTemplate["location"], &stTmpl.oRect)) {
            LOGE("load rect failed when load template");
            return -1;
        }

        stTmpl.fThreshold = oJsonTemplate["threshold"].asFloat();

        if (!oJsonTemplate["mask"].isNull()) {
            stTmpl.strMaskPath = g_userCfgPath + oJsonTemplate["mask"].asString();
        }

        stTmpl.strTmplPath = g_userCfgPath + oJsonTemplate["path"].asString();
        stTmpl.strTmplName = oJsonTemplate["name"].asString();
        stTmpl.nClassID = oJsonTemplate["classID"].asInt();

        oVecTemplates->push_back(stTmpl);
    }

    return 0;
}

void CGameRegFrameWork::Release() {
    // 释放资源前先等待所有线程执行完
    if (m_oThreadPoolMgr.GetSholdRelease()) {
        m_oThreadPoolMgr.WaitAllTaskFinish();
    }

    // 释放结果队列中的资源
    while (!g_oTaskResultQueue.IsEmpty()) {
        CTaskResult oTaskResult;
        g_oTaskResultQueue.TryPop(&oTaskResult);
        oTaskResult.Release();
    }

    //    g_oTaskResultQueue.Release();
        // 释放网络模块的资源
    if (m_oNetWorkManager.GetSholdRelease()) {
        m_oNetWorkManager.Release();
    }

    // 释放taskManager模块的资源
    if (m_oTaskManager.GetSholdRelease()) {
        m_oTaskManager.Release();
    }

    // 释放线程池模块的资源
    if (m_oThreadPoolMgr.GetSholdRelease()) {
        m_oThreadPoolMgr.Release();
    }

    CGameRegMemPool::getInstance()->DestroyMemPool();
}

void CGameRegFrameWork::SetExited() {
    m_bExited = true;
}

int CGameRegFrameWork::LoadConfg(int *nThreadNum, char* pszSysCfgPath, char* pszUserCfgFilePath) {
    // 读取配置文件
    CIniConfig oIniConfig;
    char szPath[256] = { 0 };
    sprintf(szPath, "%s%s", pszSysCfgPath, GAMEREG_PLATFORM_CFG);

    int        nRst = oIniConfig.loadFile(szPath);

    if (nRst != 0) {
        LOGE("load file %s failed", szPath);
        return -1;
    }

    // 读取日志路径
    char pLogPath[256];
    if (-1 == oIniConfig.getPrivateStr("LOG", "Path", "../log/GameRecognize/GameReg.log",
        pLogPath, 256)) {
        LOGE("get log path failed: %s", pLogPath);
        return -1;
    }

    // 读取日志级别
    char pLogLevel[64];
    if (-1 == oIniConfig.getPrivateStr("LOG", "Level", "ERROR", pLogLevel, 64)) {
        LOGE("get log level failed: %s", pLogLevel);
        return -1;
    }

    // 解析日志级别
    if (!CreateLog(pLogPath, pLogLevel)) {
        LOGE("log init failed");
        return -1;
    }

    // 读取线程个数和识别结果的最大队列长度
    *nThreadNum = oIniConfig.getPrivateInt("THREAD", "WorkCount", 20);
    g_nMaxResultSize = oIniConfig.getPrivateInt("THREAD", "MaxResultQueueSize", 20);

    LOGI("get the thread and max result size, threadNum:%d, maxResultSize:%d",
        *nThreadNum, g_nMaxResultSize);


    // ================================================================
    // 读取AI_SDK_PATH环境变量下的 GameReg_mgr配置文件
    // ================================================================
    Json::Reader reader;
    Json::Value  root;

    std::ifstream iFile;
    iFile.open(pszUserCfgFilePath);

    // std::string projectCfg = g_userCfgPath + PROJECT_CFG;
    // iFile.open(projectCfg);
    if (!iFile.is_open()) {
        LOGE("can not open : %s", pszUserCfgFilePath);
    }

    if (!reader.parse(iFile, root)) {
        LOGE("can not read json content: %s", pszUserCfgFilePath);
        return -1;
    }

    if (root["multi_resolution"].isNull()) {
        LOGE("key of 'multi_resolution' is needed in %s", pszUserCfgFilePath);
    }

    m_bTestFlag = oIniConfig.getPrivateBool("DEBUG", "Flag", 0);

    char pTestVideo[64];
    if (-1 == oIniConfig.getPrivateStr("DEBUG", "Video", "./test/sendMC.mp4", pTestVideo, 64)) {
        LOGW("get video failed %s", pTestVideo);
    } else {
        m_strTestVideo = pTestVideo;
    }

    m_nFrameRate = oIniConfig.getPrivateInt("DEBUG", "Framerate", 20);
    LOGI("get frame reate parameter, m_nFrameRate:%d", m_nFrameRate);

    m_bMultiResolution = root["multi_resolution"].asBool();
    LOGI("get the mulit resolution parameter, m_bMultiResolution:%d", m_bMultiResolution);

    oIniConfig.closeFile();
    return 0;
}

void CGameRegFrameWork::SetTestMode(bool bDebug, ETestMode eTestMode) {
    m_bTestFlag = bDebug;
    m_eTestMode = eTestMode;
    m_oTaskManager.SetTestMode(bDebug, eTestMode);
}
/*! @} */
