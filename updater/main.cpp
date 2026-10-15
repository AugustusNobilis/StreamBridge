// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.hpp"
#include <commctrl.h>
#include <fstream>
#include <shellapi.h>
#include <shobjidl.h>
#include <thread>
#include <winrt/base.h>

// clang-format off
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
// clang-format on
namespace {
namespace fs = std::filesystem;
fs::path chooseFolder(const fs::path &initial) {
    winrt::com_ptr<IFileDialog> dialog;
    winrt::check_hresult(
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())));
    DWORD options{};
    winrt::check_hresult(dialog->GetOptions(&options));
    winrt::check_hresult(
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
    dialog->SetTitle(L"Папка OBS Studio: внутри должен быть bin\\64bit\\obs64.exe");
    if (!initial.empty()) {
        winrt::com_ptr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(item.put()))))
            dialog->SetFolder(item.get());
    }
    if (dialog->Show(nullptr) == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    winrt::com_ptr<IShellItem> selected;
    winrt::check_hresult(dialog->GetResult(selected.put()));
    PWSTR path{};
    winrt::check_hresult(selected->GetDisplayName(SIGDN_FILESYSPATH, &path));
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}
struct Job {
    fs::path root, cache;
    std::atomic_bool cancel{false}, done{false}, installing{false};
    std::wstring error;
    std::thread worker;
};
HRESULT CALLBACK callback(HWND window, UINT notification, WPARAM button, LPARAM, LONG_PTR context) {
    auto &job = *reinterpret_cast<Job *>(context);
    if (notification == TDN_CREATED) {
        SendMessageW(window, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
        job.worker = std::thread([&job, window] {
            bootstrap::Download download;
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                auto progress = [window](const std::wstring &message, int percent) {
                    SendMessageW(window, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
                                 reinterpret_cast<LPARAM>(message.c_str()));
                    SendMessageW(window, TDM_SET_PROGRESS_BAR_POS, percent, 0);
                };
                download = bootstrap::download(job.cache, job.cancel, progress);
                if (job.cancel)
                    throw bootstrap::Failure{L"Обновление отменено."};
                job.installing = true;
                SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, FALSE);
                if (job.cancel)
                    throw bootstrap::Failure{L"Обновление отменено."};
                bootstrap::install(download, job.root, progress, window);
            } catch (const bootstrap::Failure &failure) {
                job.error = failure.message;
            } catch (...) {
                job.error = L"Обновление не завершено. Проверьте интернет, "
                            L"свободное место и папку OBS.";
            }
            bootstrap::cleanup(download);
            job.done = true;
            SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, TRUE);
            PostMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
        });
    } else if (notification == TDN_BUTTON_CLICKED && button == IDCANCEL && !job.done) {
        if (!job.installing) {
            job.cancel = true;
            SendMessageW(window, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
                         reinterpret_cast<LPARAM>(L"Отмена загрузки…"));
        }
        return S_FALSE;
    }
    return S_OK;
}
int runGui() {
    auto root = bootstrap::findObs();
    for (;;) {
        const auto description =
            (root.empty() ? L"OBS не найден. Выберите папку установки." : L"Папка OBS:\n" + root.wstring()) +
            std::wstring(L"\n\nОбновление 0.1.x / 0.2.0–0.2.4 → 0.2.5 через "
                         L"GitHub.\nНастройки и ключи сохраняются. "
                         L"Потребуется интернет. Если OBS работает, обновление "
                         L"дождётся его закрытия.");
        TASKDIALOG_BUTTON buttons[] = {{100, L"Обновить до 0.2.5"}, {101, L"Выбрать папку OBS…"}};
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        dialog.pszWindowTitle = L"StreamBridge Updater";
        dialog.pszMainInstruction = L"Обновление StreamBridge до 0.2.5";
        dialog.pszContent = description.c_str();
        dialog.dwFlags = TDF_SIZE_TO_CONTENT;
        dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        dialog.cButtons = root.empty() ? 1 : 2;
        dialog.pButtons = root.empty() ? &buttons[1] : buttons;
        dialog.nDefaultButton = root.empty() ? 101 : 100;
        int answer{};
        winrt::check_hresult(TaskDialogIndirect(&dialog, &answer, nullptr, nullptr));
        if (answer == IDCANCEL)
            return 0;
        if (answer == 101) {
            auto chosen = chooseFolder(root);
            if (!chosen.empty()) {
                if (bootstrap::isObsRoot(chosen))
                    root = fs::canonical(chosen);
                else
                    MessageBoxW(nullptr, L"В этой папке нет bin\\64bit\\obs64.exe.", L"StreamBridge",
                                MB_ICONERROR);
            }
            continue;
        }
        break;
    }
    Job job;
    job.root = root;
    job.cache = fs::temp_directory_path();
    TASKDIALOGCONFIG progress{sizeof(progress)};
    progress.pszWindowTitle = L"StreamBridge Updater";
    progress.pszMainInstruction = L"Обновление до 0.2.5";
    progress.pszContent = L"Подключение к GitHub…";
    progress.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_SIZE_TO_CONTENT;
    progress.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    progress.pfCallback = callback;
    progress.lpCallbackData = reinterpret_cast<LONG_PTR>(&job);
    const HRESULT result = TaskDialogIndirect(&progress, nullptr, nullptr, nullptr);
    if (job.worker.joinable())
        job.worker.join();
    winrt::check_hresult(result);
    if (!job.error.empty()) {
        MessageBoxW(nullptr, job.error.c_str(), L"StreamBridge — обновление", MB_OK | MB_ICONERROR);
        return 1;
    }
    MessageBoxW(nullptr,
                L"StreamBridge обновлён до 0.2.5. Теперь можно запустить OBS.\n"
                L"Следующие обновления доступны в настройках StreamBridge → Обновления.",
                L"Обновление завершено", MB_OK | MB_ICONINFORMATION);
    return 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool commandLine = false;
    fs::path report;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
        int argc{};
        auto args = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!args)
            return 1;
        std::wstring mode = argc > 1 ? args[1] : L"";
        fs::path target = argc > 2 ? args[2] : L"";
        LocalFree(args);
        commandLine = !mode.empty();
        if (commandLine) {
            if (argc != 3 || (mode != L"--download-only" && mode != L"--install-to") || !target.is_absolute())
                return 2;
            // Explicit diagnostic/automation modes use the SAME fixed public release
            // and validation. There is deliberately no arbitrary URL or unverified
            // local-installer override.
            const auto cache = mode == L"--download-only" ? target : fs::temp_directory_path();
            fs::create_directories(cache);
            report = cache / L"updater-result.txt";
            if (mode == L"--install-to" && !bootstrap::isObsRoot(target))
                return 2;
            std::atomic_bool cancel{false};
            auto download = bootstrap::download(cache, cancel, [](const auto &, int) {});
            try {
                if (mode == L"--install-to")
                    bootstrap::install(download, target, [](const auto &, int) {});
                std::ofstream output(report);
                output << "verified " << winrt::to_string(download.file.wstring()) << "\n"
                       << winrt::to_string(download.release.digest) << "\n";
                if (mode == L"--install-to")
                    bootstrap::cleanup(download);
                return 0;
            } catch (...) {
                bootstrap::cleanup(download);
                throw;
            }
        }
        return runGui();
    } catch (const bootstrap::Failure &failure) {
        if (commandLine) {
            if (!report.empty())
                std::ofstream(report) << winrt::to_string(failure.message);
        } else
            MessageBoxW(nullptr, failure.message.c_str(), L"StreamBridge", MB_ICONERROR);
    } catch (...) {
        if (commandLine) {
            if (!report.empty())
                std::ofstream(report) << "Updater failed";
        } else
            MessageBoxW(nullptr, L"Не удалось запустить обновление.", L"StreamBridge", MB_ICONERROR);
    }
    return 1;
}
