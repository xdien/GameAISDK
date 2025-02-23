# -*- coding: utf-8 -*-
"""
Tencent is pleased to support the open source community by making GameAISDK available.

This source code file is licensed under the GNU General Public License Version 3.
For full details, please refer to the file "LICENSE.txt" which is provided as part of this source code package.

Copyright (C) 2020 THL A29 Limited, a Tencent company.  All rights reserved.
"""

import os
from os import environ
import json
import logging
from pathlib import Path
from .config_path_mgr import DEFAULT_USER_CONFIG_DIR
from protocol import common_pb2

logger = logging.getLogger("agent")


def get_configure(file_path):
    try:
        with open(file_path, 'r', encoding='utf-8') as file:
            json_str = file.read()
            dqn_configure = json.loads(json_str)
            return dqn_configure
    except Exception as err:
        logger.error('Load game state file {} error! Error msg: {}'.format(file_path, err))
        return None


def get_button_state(result_dict, task_id):
    #fixed object
    state = False
    px = -1
    py = -1

    reg_results = result_dict.get(task_id)
    if reg_results is None:
        return state, px, py

    for item in reg_results:
        flag = item['flag']
        if flag and 'boxes' in item:
            bb_index_at_0 = item['boxes'][0]
            x = bb_index_at_0['x']
            y = bb_index_at_0['y']
            w = bb_index_at_0['w']
            h = bb_index_at_0['h']

            state = True
            px = int(x + w/2)
            py = int(y + h/2)
            break
        else:
            logger.warn("start box not found, please reconfig sense task ")
            # raise "Wont config for start task, recomend fix object "

    return state, px, py

def get_number_dict(result_dict, task_id) -> float:
    score = 0
    reg_results = result_dict.get(task_id)
    if reg_results is None:
        return score
    for item in reg_results:
        flag = item['flag']
        if flag and 'num' in item:
            score = item['num']
            break
        else:
            logger.warn("Cant find score in box")
            # raise "Wont config for start task, recomend fix object "

    return score

def get_number_percent(result_dict, task_id) -> float:
    percent = 0
    reg_results = result_dict.get(task_id)
    if reg_results is None:
        return percent
    for item in reg_results:
        flag = item['flag']
        if flag and 'percent' in item:
            percent = item['percent']
            break
        else:
            logger.warn("Cant find number percent in box")
            # raise "Wont config for start task, recomend fix object "

    return percent


def create_source_response(source_dict):
    source_res_message = common_pb2.tagMessage()
    source_res_message.eMsgID = common_pb2.MSG_PROJECT_SOURCE_RES

    if 'device_type' in source_dict:
        source_res_message.stSource.deviceType = source_dict['device_type']
        os.environ['AISDK_DEVICE_TYPE'] = source_dict['device_type']
    if 'platform' in source_dict:
        source_res_message.stSource.platform = source_dict['platform']
    if 'long_edge' in source_dict:
        source_res_message.stSource.longEdge = source_dict['long_edge']
    if 'window_size' in source_dict:
        source_res_message.stSource.windowsSize = source_dict['window_size']
    if 'window_qpath' in source_dict:
        source_res_message.stSource.queryPath = source_dict['window_qpath']

    return source_res_message

def ConvertToSDKFilePath(filePath):
    """Convert file path according to environment variable AI_SDK_PATH

    """
    env_path = os.environ.get('AI_SDK_PROJECT_PATH', DEFAULT_USER_CONFIG_DIR)
    sdk_file_path = os.path.join(env_path, filePath)
    return sdk_file_path

def ConvertToProjectFilePath(filePath):
    """Convert file path according to environment variable AI_SDK_PATH

    """
    if environ.get('AI_SDK_PROJECT_FULL_PATH') is not None:
        env_path = os.environ.get('AI_SDK_PROJECT_FULL_PATH')
        sdk_file_path = os.path.join(env_path, filePath)
    else:
        logging.raiseExceptions("dir AI_SDK_PROJECT_FULL_PATH not set")
    return sdk_file_path

