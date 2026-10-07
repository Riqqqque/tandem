#include "pch.h"

#include <list>
#include <unordered_map>

#include "push-widget.h"
#include "plugin-support.h"

#include "output-config.h"
#include "chat-controller.h"
#include "chat-dock.h"
#include "settings-dialog.h"

static class GlobalServiceImpl : public GlobalService
{
public:
    bool RunInUIThread(std::function<void()> task) override {
        if (uiThread_ == nullptr) {
            blog(LOG_WARNING, TAG "RunInUIThread: UI thread not captured yet, dropping task.");
            return false;
        }
        QMetaObject::invokeMethod(uiThread_, [func = std::move(task)]() {
            func();
        });
        return true;
    }

    QThread* uiThread_ = nullptr;
} s_service;


GlobalService& GetGlobalService() {
    return s_service;
}

static bool s_frontendReady = false;

bool IsFrontendReady() {
    return s_frontendReady;
}


class OutputsListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

    QSize sizeHint() const override
    {
        QSize hint = QListWidget::sizeHint();
        hint.setHeight(ContentHeight());
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QListWidget::event(event);

        switch (event->type()) {
        case QEvent::FontChange:
        case QEvent::LayoutRequest:
        case QEvent::PolishRequest:
        case QEvent::Show:
        case QEvent::StyleChange:
            RefreshItemSizeHints();
            updateGeometry();
            break;
        default:
            break;
        }

        return handled;
    }

private:
    void RefreshItemSizeHints()
    {
        for (int i = 0; i < count(); ++i) {
            auto item = this->item(i);
            auto widget = itemWidget(item);
            if (item && widget)
                item->setSizeHint(widget->sizeHint());
        }
    }

    int ContentHeight() const
    {
        auto *widget = const_cast<OutputsListWidget *>(this);
        widget->doItemsLayout();

        int totalHeight = frameWidth() * 2;
        const int itemCount = count();
        for (int i = 0; i < itemCount; ++i) {
            totalHeight += sizeHintForRow(i);
        }

        if (itemCount > 1) {
            totalHeight += (itemCount - 1) * spacing();
        }

        return (std::max)(totalHeight, frameWidth() * 2);
    }
};


class MultiOutputWidget : public QWidget
{
public:
    MultiOutputWidget(QWidget* parent = 0)
        : QWidget(parent)
    {
        setWindowTitle(obs_module_text("Title"));

        container_ = new QWidget(&scroll_);
        layout_ = new QVBoxLayout(container_);
        layout_->setAlignment(Qt::AlignmentFlag::AlignTop);
        layout_->setSizeConstraint(QLayout::SetMinAndMaxSize);

        // init widget
        auto addButton = new QPushButton(obs_module_text("Btn.NewTarget"), container_);
        QObject::connect(addButton, &QPushButton::clicked, [this]() {
            auto& global = GlobalMultiOutputConfig();
            auto newId = GenerateId(global);
            auto target = std::make_shared<OutputTargetConfig>();
            target->id = newId;
            global.targets.emplace_back(target);
            auto pushWidget = AddPushWidget(newId);
            if (pushWidget->ShowEditDlg()) {
                SaveConfig();
            } else {
                DeletePushWidget(newId);
            }
        });
        layout_->addWidget(addButton);

        // start enabled, stop all
        auto allBtnContainer = new QWidget(this);
        auto allBtnLayout = new QHBoxLayout();
        allBtnLayout->setContentsMargins(0, 0, 0, 0);
        auto startAllButton = new QPushButton(obs_module_text("Btn.StartEnabled"), allBtnContainer);
        startAllButton->setToolTip(obs_module_text("Tip.StartEnabled"));
        allBtnLayout->addWidget(startAllButton);
        auto stopAllButton = new QPushButton(obs_module_text("Btn.StopAll"), allBtnContainer);
        allBtnLayout->addWidget(stopAllButton);
        allBtnContainer->setLayout(allBtnLayout);
        layout_->addWidget(allBtnContainer);

        QObject::connect(startAllButton, &QPushButton::clicked, [this]() {
            int started = 0;
            for (auto x : GetAllPushWidgets()) {
                if (x->IsEnabled()) {
                    x->StartStreaming();
                    ++started;
                }
            }
            if (started == 0) {
                QMessageBox::information(this, obs_module_text("Title"), obs_module_text("Notice.NoneEnabled"));
            }
        });
        QObject::connect(stopAllButton, &QPushButton::clicked, [this]() {
            for (auto x : GetAllPushWidgets())
                x->StopStreaming();
        });

        // upload estimate
        bandwidth_ = new QLabel(container_);
        bandwidth_->setWordWrap(true);
        bandwidth_->setTextFormat(Qt::PlainText);
        layout_->addWidget(bandwidth_);
        bandwidthTimer_ = new QTimer(this);
        bandwidthTimer_->setInterval(1000);
        QObject::connect(bandwidthTimer_, &QTimer::timeout, [this]() { UpdateBandwidth(); });
        bandwidthTimer_->start();

        // load and show outputs
        outputsContainer_ = new OutputsListWidget(container_);
        outputsContainer_->setDragDropMode(QAbstractItemView::InternalMove);
        outputsContainer_->setSelectionMode(QAbstractItemView::SingleSelection);
        outputsContainer_->setDropIndicatorShown(true);
        outputsContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        outputsContainer_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        outputsContainer_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outputsContainer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        outputsContainer_->setStyleSheet(
            "QListWidget {"
            "   padding: 0px;"
            "   margin: 0px;"
            "   border: none;"
            "   background: transparent;"
            "}"
            "QListWidget::item:selected, QListWidget::item:active {"
            "   border: none;"
            "   background: transparent;"
            "}"
            "QListWidget::item:hover, QListWidget::item:hover:selected, QListWidget::item:hover:active {"
            "   border: none;"
            "   background: rgba(127, 127, 127, 0.1);"
            "   cursor: move;"
            "}"
        );
        LoadConfig();
        connect(
            outputsContainer_->model(),
            &QAbstractItemModel::rowsMoved,
            this,
            &MultiOutputWidget::OnOutputMoved
        );
        layout_->addWidget(outputsContainer_);

        // settings + credits
        {
            auto footer = new QHBoxLayout();
            auto settingsBtn = new QPushButton(obs_module_text("Btn.Settings"), container_);
            QObject::connect(settingsBtn, &QPushButton::clicked, [this]() {
                ShowTandemSettings(this);
                UpdateBandwidth();
            });
            footer->addWidget(settingsBtn);
            footer->addStretch(1);
            layout_->addLayout(footer);

            auto label = new QLabel(QString::fromUtf8(
                "<p>Tandem %1 by Rique<br>"
                "Based on <a href=\"https://github.com/sorayuki/obs-multi-rtmp\">obs-multi-rtmp</a> by SoraYuki</p>")
                .arg(QString::fromUtf8(PLUGIN_VERSION)),
                container_);
            label->setTextFormat(Qt::RichText);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            layout_->addWidget(label);
        }

        scroll_.setWidgetResizable(true);
        scroll_.setWidget(container_);

        auto fullLayout = new QGridLayout(this);
        fullLayout->setContentsMargins(0, 0, 0, 0);
        fullLayout->setRowStretch(0, 1);
        fullLayout->setColumnStretch(0, 1);
        fullLayout->addWidget(&scroll_, 0, 0);

        UpdateBandwidth();
    }

    std::list<PushWidget*> GetAllPushWidgets()
    {
        std::list<PushWidget*> result;
        for (int row = 0; row < outputsContainer_->count(); ++row) {
            auto item = outputsContainer_->item(row);
            if (!item) {
                continue;
            }

            auto widget = outputsContainer_->itemWidget(item);
            auto pushWidget = dynamic_cast<PushWidget*>(widget);
            if (pushWidget) {
                result.push_back(pushWidget);
            }
        }
        return result;
    }

    void SaveConfig()
    {
        SaveMultiOutputConfig();
    }

    void OnOutputMoved(
        const QModelIndex &parent,
        int start,
        int end,
        const QModelIndex &destination,
        int row
    )
    {
        // QListWidget uses a single root parent for internal move operations.
        if (parent != destination) {
            return;
        }

        const int count = outputsContainer_->count();
        if (count <= 0 || start < 0 || end < start || row < 0 || row > count) {
            return;
        }

        auto &targets = GlobalMultiOutputConfig().targets;
        std::unordered_map<std::string, OutputTargetConfigPtr> targetById;
        targetById.reserve(targets.size());
        for (auto &target : targets) {
            if (target) {
                targetById.emplace(target->id, target);
            }
        }

        std::remove_reference_t<decltype(targets)> reordered;
        for (int i = 0; i < count; ++i) {
            auto item = outputsContainer_->item(i);
            if (!item) {
                continue;
            }

            auto id = tostdu8(item->data(Qt::UserRole).toString());
            auto it = targetById.find(id);
            if (it == targetById.end()) {
                continue;
            }

            reordered.emplace_back(it->second);
            targetById.erase(it);
        }

        // Keep unmatched items in their previous order to avoid accidental loss.
        if (!targetById.empty()) {
            for (auto &target : targets) {
                if (!target) {
                    continue;
                }
                auto it = targetById.find(target->id);
                if (it == targetById.end()) {
                    continue;
                }
                reordered.emplace_back(it->second);
                targetById.erase(it);
            }
        }

        targets.swap(reordered);

        SaveConfig();
        outputsContainer_->clearSelection();
    }

    void LoadConfig()
    {
        // QListWidget::clear() deletes the items but not their widgets; stop and
        // delete the old push widgets so nothing keeps streaming across a profile switch.
        for (auto x : GetAllPushWidgets()) {
            x->Stop();
            x->deleteLater();
        }
        outputsContainer_->clear();

        GlobalMultiOutputConfig() = {};
        LoadMultiOutputConfig();

        for(auto x: GlobalMultiOutputConfig().targets)
        {
            AddPushWidget(x->id);
        }
        UpdateBandwidth();
    }

    void UpdateBandwidth()
    {
        if (!bandwidth_ || !outputsContainer_ || !IsFrontendReady())
            return;

        double nowKbps = 0;
        double plannedKbps = 0;
        int active = 0, enabled = 0, ownEncoders = 0;
        for (auto x : GetAllPushWidgets()) {
            bool busy = x->IsBusy();
            if (busy) {
                ++active;
                nowKbps += x->CurrentKbps();
            }
            if (busy || x->IsEnabled()) {
                ++enabled;
                plannedKbps += x->EstimatedKbps();
                if (x->UsesOwnVideoEncoder())
                    ++ownEncoders;
            }
        }
        bool mainLive = obs_frontend_streaming_active();
        double mainKbps = EstimateMainStreamKbps();
        if (mainLive)
            plannedKbps += mainKbps;

        auto mbps = [](double kbps) { return QString::number(kbps / 1000.0, 'f', 1); };
        QString text = QString::fromUtf8(obs_module_text("Bandwidth.Summary"))
            .arg(mbps(plannedKbps))
            .arg(enabled + (mainLive ? 1 : 0));
        if (active > 0)
            text += "\n" + QString::fromUtf8(obs_module_text("Bandwidth.Now")).arg(mbps(nowKbps));
        if (ownEncoders > 0)
            text += "\n" + QString::fromUtf8(obs_module_text("Bandwidth.ExtraEncoders")).arg(ownEncoders);

        double capacity = GlobalMultiOutputConfig().uploadCapacityMbps * 1000.0;
        bool warn = capacity > 0 && plannedKbps > capacity * 0.8;
        if (warn)
            text += "\n" + QString::fromUtf8(obs_module_text("Bandwidth.Warning")).arg(mbps(capacity));
        bandwidth_->setText(text);
        bandwidth_->setStyleSheet(warn ? "color: #e5534b;" : "");
    }

private:
    // Main widget of this module's dock
    QWidget* container_ = 0;
    // The layout of the root widget
    QVBoxLayout* layout_ = 0;
    // Scrollable area in case of overflows of content
    QScrollArea scroll_;
    // Widget, that contains output source widgets
    QListWidget* outputsContainer_ = 0;
    QLabel* bandwidth_ = 0;
    QTimer* bandwidthTimer_ = 0;

    void DeletePushWidget(const std::string& targetId)
    {
        // Delete from model
        auto outputTargets = &(GlobalMultiOutputConfig().targets);
        auto currentTarget = std::find_if(outputTargets->begin(), outputTargets->end(), [&targetId](auto& x) { return x->id == targetId; });
        if (currentTarget == outputTargets->end()) {
            return;
        }
        outputTargets->erase(currentTarget);
        RemoveTargetSecrets(targetId);

        // Delete from List View
        const QString id = QString::fromStdString(targetId);
        for (int row = outputsContainer_->count() - 1; row >= 0; --row) {
            auto listItem = outputsContainer_->item(row);
            if (!listItem || listItem->data(Qt::UserRole).toString() != id) {
                continue;
            }
            auto pushWidget = dynamic_cast<PushWidget*>(outputsContainer_->itemWidget(listItem));
            if (pushWidget)
                pushWidget->Stop();
            auto removedItem = outputsContainer_->takeItem(row);
            delete removedItem;
            if (pushWidget) {
                pushWidget->deleteLater();
            }
        }
    }

    PushWidget* AddPushWidget(const std::string& targetId)
    {
        auto pushWidget = createPushWidget(targetId, outputsContainer_->viewport());

        QListWidgetItem* listItem = new QListWidgetItem();
        listItem->setData(Qt::UserRole, QString::fromStdString(targetId));
        listItem->setSizeHint(pushWidget->sizeHint());
        outputsContainer_->addItem(listItem);
        outputsContainer_->setItemWidget(listItem, pushWidget);

        QObject::connect(pushWidget->GetDeleteButton(), &QPushButton::clicked, [this, targetId]() {
            QMessageBox msgbox(
                QMessageBox::Icon::Question,
                obs_module_text("Question.Title"),
                obs_module_text("Question.Delete"),
                QMessageBox::Yes | QMessageBox::No,
                this
            );
            if (msgbox.exec() != QMessageBox::Yes) {
                return;
            }
            DeletePushWidget(targetId);
            SaveConfig();
        });

        return pushWidget;
    }
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("tandem", "en-US")
OBS_MODULE_AUTHOR("Rique")

static void ForwardChatLog(tandem::chat::LogLevel level, const char* msg)
{
    int obsLevel = LOG_INFO;
    switch (level) {
    case tandem::chat::LogLevel::Debug: obsLevel = LOG_DEBUG; break;
    case tandem::chat::LogLevel::Info: obsLevel = LOG_INFO; break;
    case tandem::chat::LogLevel::Warning: obsLevel = LOG_WARNING; break;
    case tandem::chat::LogLevel::Error: obsLevel = LOG_ERROR; break;
    }
    blog(obsLevel, TAG "%s", msg);
}

bool obs_module_load()
{
    auto mainwin = (QMainWindow*)obs_frontend_get_main_window();
    if (mainwin == nullptr)
        return false;
    QMetaObject::invokeMethod(mainwin, []() {
        s_service.uiThread_ = QThread::currentThread();
    });

    tandem::chat::SetLogFunction(&ForwardChatLog);

    auto dock = new MultiOutputWidget();
    dock->setObjectName("tandem-outputs-dock");
    if (!obs_frontend_add_dock_by_id("tandem-outputs-dock", obs_module_text("Title"), dock))
    {
        delete dock;
        return false;
    }

    auto chatDock = CreateChatDock();
    if (!obs_frontend_add_dock_by_id("tandem-chat-dock", obs_module_text("Chat.Title"), chatDock))
    {
        blog(LOG_WARNING, TAG "Could not add the chat dock.");
        delete chatDock;
    }

    blog(LOG_INFO, TAG "version %s loaded (based on obs-multi-rtmp by SoraYuki)", PLUGIN_VERSION);

    obs_frontend_add_event_callback(
        [](enum obs_frontend_event event, void *private_data) {
            auto dock = static_cast<MultiOutputWidget*>(private_data);

            if (event == obs_frontend_event::OBS_FRONTEND_EVENT_FINISHED_LOADING) {
                s_frontendReady = true;
                dock->UpdateBandwidth();
                // New docks start hidden; show ours once so first-time users find them.
                auto userConfig = obs_frontend_get_user_config();
                if (userConfig && !config_get_bool(userConfig, "Tandem", "DocksShown")) {
                    auto mainwin = (QMainWindow*)obs_frontend_get_main_window();
                    for (auto name : { "tandem-outputs-dock", "tandem-chat-dock" }) {
                        // OBS names the QDockWidget after the id; fall back to our widget's parent.
                        auto qdock = mainwin->findChild<QDockWidget*>(name);
                        if (!qdock) {
                            for (auto widget : mainwin->findChildren<QWidget*>(name)) {
                                if ((qdock = qobject_cast<QDockWidget*>(widget->parentWidget())))
                                    break;
                            }
                        }
                        if (qdock) {
                            qdock->setFloating(false);
                            qdock->show();
                            qdock->raise();
                        } else {
                            blog(LOG_WARNING, TAG "Could not find dock %s to show it.", name);
                        }
                    }
                    config_set_bool(userConfig, "Tandem", "DocksShown", true);
                    config_save_safe(userConfig, "tmp", nullptr);
                }
            } else if (event == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT) {
                s_frontendReady = false;
            }

            for(auto x: dock->GetAllPushWidgets())
                x->OnOBSEvent(event);

            GetChatController().OnFrontendEvent(event);

            if (event == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT)
            {
                dock->SaveConfig();
                GetChatController().Shutdown();
            }
            else if (event == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_CHANGED)
            {
                dock->LoadConfig();
                GetChatController().ApplyConfig();
            }
        }, dock
    );

    return true;
}

void obs_module_unload()
{
    GetChatController().Shutdown();
}

const char *obs_module_name(void)
{
    return "Tandem";
}

const char *obs_module_description(void)
{
    return "Multistream to several platforms from one encode, with a unified chat dock.";
}
