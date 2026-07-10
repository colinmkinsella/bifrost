#include "bifrostmanagedialog.h"
#include "bifrostdiffstore.h"
#include "bifrostdiffview.h"
#include "uicontext.h"

#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

using namespace BinaryNinja;

// Item data role holding the diff's stored name.
static constexpr int RoleDiffName = Qt::UserRole;

static Ref<Project> currentProject()
{
    UIContext* ctx = UIContext::activeContext();
    if (!ctx) return nullptr;
    Ref<Project> project = ctx->getProject();
    return (project && project->IsOpen()) ? project : nullptr;
}

// "left  →  right   ·   timestamp"  (best-effort; falls back to just the name)
static QString subtitleFor(Ref<Metadata> diffData)
{
    if (!diffData || !diffData->IsKeyValueStore()) return {};
    auto get = [&](const char* k) -> QString {
        auto m = diffData->Get(k);
        return (m && m->IsString()) ? QString::fromStdString(m->GetString()) : QString();
    };
    QString left = get("left"), right = get("right"), ts = get("timestamp");
    QString s;
    if (!left.isEmpty() || !right.isEmpty()) s = left + "  →  " + right;
    if (!ts.isEmpty()) s += (s.isEmpty() ? "" : "   ·   ") + ts;
    return s;
}

BifrostManageDiffsDialog::BifrostManageDiffsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Bifrost – Diffs");
    setMinimumSize(420, 280);

    auto* layout = new QVBoxLayout(this);

    auto* heading = new QLabel("Saved diffs in this project:", this);
    layout->addWidget(heading);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    auto* btnRow = new QHBoxLayout();
    m_openBtn   = new QPushButton("Open", this);
    m_deleteBtn = new QPushButton("Delete…", this);
    btnRow->addWidget(m_openBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton("Close", this);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &BifrostManageDiffsDialog::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { openSelected(); });
    connect(m_openBtn,   &QPushButton::clicked, this, &BifrostManageDiffsDialog::openSelected);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BifrostManageDiffsDialog::deleteSelected);
    connect(closeBtn,    &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void BifrostManageDiffsDialog::refresh()
{
    m_list->clear();

    Ref<Project> project = currentProject();
    if (!project)
    {
        updateButtons();
        return;
    }

    for (auto& name : bifrostListDiffs(project))
    {
        QString qname = QString::fromStdString(name);
        QString sub   = subtitleFor(bifrostLoadDiff(name, project));

        auto* item = new QListWidgetItem(m_list);
        item->setText(sub.isEmpty() ? qname : (qname + "\n" + sub));
        item->setData(RoleDiffName, qname);
    }
    updateButtons();
}

void BifrostManageDiffsDialog::updateButtons()
{
    bool hasSelection = m_list->currentItem() != nullptr;
    m_openBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void BifrostManageDiffsDialog::openSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    UIContext* ctx = UIContext::activeContext();
    Ref<Project> project = currentProject();
    if (!ctx || !project) return;

    auto diffData = bifrostLoadDiff(name.toStdString(), project);
    if (!diffData) return;

    QString tabTitle = QString("Diff: %1").arg(name);
    auto* view = new BifrostDiffView(nullptr, diffData, tabTitle);
    ctx->createTabForWidget(tabTitle, view);
    accept();
}

void BifrostManageDiffsDialog::deleteSelected()
{
    auto* item = m_list->currentItem();
    if (!item) return;
    QString name = item->data(RoleDiffName).toString();

    Ref<Project> project = currentProject();
    if (!project) return;

    auto reply = QMessageBox::question(
        this, "Delete diff",
        QString("Delete the saved diff \"%1\" from this project?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    bifrostDeleteDiff(name.toStdString(), project);
    refresh();
}
