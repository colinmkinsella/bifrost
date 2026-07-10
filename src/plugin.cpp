#include "bifrost.h"
#include "bifrostdiffdialog.h"
#include "bifrostdiffstore.h"
#include "bifrostdiffview.h"
#include "bifrostsidebar.h"
#include "action.h"
#include "uicontext.h"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

extern "C"
{
    BN_DECLARE_UI_ABI_VERSION

    BINARYNINJAPLUGIN bool UIPluginInit()
    {
        BifrostViewType::init();
        BifrostSidebarWidgetType::init();

        // ── Plugins → Bifrost → Run Diff… ──────────────────────────────────
        UIAction::registerAction("Bifrost\\Run Diff...");
        UIActionHandler::globalActions()->bindAction(
            "Bifrost\\Run Diff...",
            UIAction(
                [](const UIActionContext& ctx) {
                    QWidget* parent = ctx.context ? ctx.context->mainWindow() : nullptr;
                    BifrostDiffDialog dlg(parent);
                    dlg.exec();
                },
                [](const UIActionContext& ctx) -> bool {
                    auto* uiCtx = ctx.context;
                    if (!uiCtx) return false;
                    auto project = uiCtx->getProject();
                    return project && project->IsOpen();
                }));
        Menu::mainMenu("Plugins")->addAction("Bifrost\\Run Diff...", "Bifrost");

        // ── Plugins → Bifrost → View Diff… ─────────────────────────────────
        UIAction::registerAction("Bifrost\\View Diff...");
        UIActionHandler::globalActions()->bindAction(
            "Bifrost\\View Diff...",
            UIAction(
                [](const UIActionContext& ctx) {
                    UIContext* uiCtx = ctx.context;
                    if (!uiCtx) return;
                    auto project = uiCtx->getProject();
                    if (!project || !project->IsOpen()) return;

                    auto diffs = bifrostListDiffs(project);
                    if (diffs.empty()) return;

                    QWidget* parent = uiCtx->mainWindow();

                    // If only one diff, open it directly
                    std::string chosen = diffs[0];
                    if (diffs.size() > 1)
                    {
                        // Show a small picker dialog
                        QDialog dlg(parent);
                        dlg.setWindowTitle("Bifrost – View Diff");
                        dlg.setMinimumWidth(300);
                        auto* layout  = new QVBoxLayout(&dlg);
                        auto* combo   = new QComboBox(&dlg);
                        for (auto& name : diffs)
                            combo->addItem(QString::fromStdString(name));
                        auto* form = new QFormLayout();
                        form->addRow("Diff:", combo);
                        layout->addLayout(form);
                        auto* buttons = new QDialogButtonBox(
                            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
                        QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
                        QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
                        layout->addWidget(buttons);
                        if (dlg.exec() != QDialog::Accepted) return;
                        chosen = combo->currentText().toStdString();
                    }

                    auto diffData = bifrostLoadDiff(chosen, project);
                    if (!diffData) return;

                    QString tabTitle = QString("Diff: %1").arg(QString::fromStdString(chosen));
                    auto* view = new BifrostDiffView(nullptr, diffData, tabTitle);
                    uiCtx->createTabForWidget(tabTitle, view);
                },
                [](const UIActionContext& ctx) -> bool {
                    auto* uiCtx = ctx.context;
                    if (!uiCtx) return false;
                    auto project = uiCtx->getProject();
                    if (!project || !project->IsOpen()) return false;
                    return !bifrostListDiffs(project).empty();
                }));
        Menu::mainMenu("Plugins")->addAction("Bifrost\\View Diff...", "Bifrost");

        return true;
    }
}
