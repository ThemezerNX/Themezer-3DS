// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Themezer-3DS contributors

#include "services/reporter.hpp"

#include "app/strings.hpp"
#include "app_version.h"
#include "domain/report.hpp"
#include "services/themezer.hpp"
#include "sys/jobs.hpp"
#include "sys/log.hpp"
#include "sys/net.hpp"
#include "ui/overlays.hpp"

#include <3ds.h>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

namespace tz::report
{

namespace
{

const char* modelName(u8 model)
{
    constexpr const char* kNames[] = {"3DS", "3DS XL", "New 3DS", "2DS", "New 3DS XL", "New 2DS XL"};
    return model < std::size(kNames) ? kNames[model] : "unknown";
}

const char* regionName(u8 region)
{
    constexpr const char* kNames[] = {"JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN"};
    return region < std::size(kNames) ? kNames[region] : "unknown";
}

Report collect(const Error& error)
{
    Report report;
    report.version = APP_VERSION;
    u8 model = 0xFF;
    u8 region = 0xFF;
    CFGU_GetSystemModel(&model);
    CFGU_SecureInfoGetRegion(&region);
    report.device = modelName(model);
    report.region = regionName(region);
    // as System Settings shows it, "11.17.0-50E"
    char version[32];
    report.firmware = R_SUCCEEDED(osGetSystemVersionDataString(nullptr, nullptr, version, sizeof(version)))
                          ? version
                          : "unknown";
    report.error = error;
    report.log = log::tail(SIZE_MAX); // all the kept lines
    return report;
}

} // namespace

void share(const Error& error)
{
    // collected now, on the main thread: the log as it was when the button was pressed, and the system version, which
    // libctru reads by mounting the version title's RomFS (as ver:), better kept off the workers
    std::string body = toJson(collect(error));
    jobs::async(
        Lane::Network,
        [body = std::move(body)](const CancelToken& token) -> Expected<void> {
            net::Request request;
            request.url = std::string(themezer::kApiUrl) + "/reports";
            request.cancel = &token;
            std::unique_ptr<net::Client> client = net::takeClient();
            Expected<net::Response> sent = client->post(request, body, "application/json");
            net::giveBack(std::move(client));
            if (!sent)
                return fail(sent.error());
            return {};
        },
        [](Expected<void> sent) {
            if (sent)
            {
                ui::toast(tr("Thanks, the error was shared"), ui::ToastKind::Success);
                return;
            }
            // not through showError(): that would offer to share this one too
            LOG_WARN("sharing the error failed: %s", sent.error().detail.c_str());
            ui::toast(tr("Couldn't share the error"), ui::ToastKind::Warning);
        });
}

} // namespace tz::report
