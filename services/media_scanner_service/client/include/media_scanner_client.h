/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MEDIA_SCANNER_CLIENT_H
#define MEDIA_SCANNER_CLIENT_H

#include <list>
#include <stdint.h>
#include <string>
#include <refbase.h>
#include <unordered_map>
#include <mutex>

#include "media_scanner_const.h"
#include "imedia_scanner_client.h"
#include "imedia_scanner_ability.h"
#include "media_scanner_operation_callback_stub.h"

#include "want.h"
#include "string_ex.h"
#include "media_log.h"
#include "sys_mgr_client.h"
#include "ability_manager_interface.h"
#include "system_ability_definition.h"
#include "ability_connect_callback_interface.h"
#include "ability_connect_callback_stub.h"

namespace OHOS {
namespace Media {
struct ScanRequest {
    ScanType scanType;
    std::string path;
    sptr<IRemoteObject> serviceCb;
    std::shared_ptr<IMediaScannerAppCallback> appCallback;
};

class MediaScannerClient : public IMediaScannerClient, public RefBase {
public:
    MediaScannerClient() = default;
    ~MediaScannerClient();

    void Release() override;
    ScanState ScanDir(std::string &scanDirPath, const std::shared_ptr<IMediaScannerAppCallback> &appCb) override;
    ScanState ScanFile(std::string &scanFilePath, const std::shared_ptr<IMediaScannerAppCallback> &appCb) override;

    void OnDisconnectAbility();
    void OnConnectAbility(const sptr<IRemoteObject> &remoteObject, int32_t result);
    int32_t Init();

private:
    bool UpdateScanRequestQueue(ScanRequest &scanRequest);
    void EmptyScanRequestQueue(bool isConnected);
    bool IsScannerServiceConnected();
    void SetConnectionState(ConnectionState connState);
    ConnectionState GetConnectionState();
    ScanState ScanInternal(std::string &path, const std::shared_ptr<IMediaScannerAppCallback> &appCb, ScanType type);

    sptr<AAFwk::IAbilityConnection> connection_;
    sptr<AAFwk::IAbilityManager> abilityMgrProxy_;
    ConnectionState connectionState_ = CONN_NONE;

    std::mutex mutex_;
    sptr<IMediaScannerAbility> abilityProxy_;
    std::list<ScanRequest> scanList_;
    std::shared_ptr<IMediaScannerAppCallback> applicationCb_;
};

class MediaScannerConnectCallbackStub : public  OHOS::AAFwk::AbilityConnectionStub {
public:
    MediaScannerConnectCallbackStub(const sptr<MediaScannerClient> &msInstance);
    virtual ~MediaScannerConnectCallbackStub() = default;

    void OnAbilityDisconnectDone(const AppExecFwk::ElementName &element, int32_t resultCode) override;
    void OnAbilityConnectDone(const AppExecFwk::ElementName &element, const sptr<IRemoteObject> &remoteObject,
            int32_t resultCode) override;

private:
    sptr<MediaScannerClient> msInstance_;
};
} // namespace Media
} // namespace OHOS
#endif // MEDIA_SCANNER_CLIENT_H