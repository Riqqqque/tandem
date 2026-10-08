// SPDX-License-Identifier: GPL-2.0-or-later
#include "pch.h"

#include "simple-setup.h"
#include "auto-encoder.h"
#include "chat-controller.h"
#include "output-config.h"
#include "platforms.h"
#include "push-widget.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLocale>
#include <QUrl>

namespace {

QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QLineEdit *MakeSecretEdit(QWidget *parent, QHBoxLayout **row)
{
	auto edit = new QLineEdit(parent);
	edit->setEchoMode(QLineEdit::Password);
	auto show = new QPushButton(Text("Btn.Show"), parent);
	show->setCheckable(true);
	show->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	QObject::connect(show, &QPushButton::toggled, [edit, show](bool on) {
		edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		show->setText(Text(on ? "Btn.Hide" : "Btn.Show"));
	});
	*row = new QHBoxLayout();
	(*row)->setContentsMargins(0, 0, 0, 0);
	(*row)->addWidget(edit, 1);
	(*row)->addWidget(show);
	return edit;
}

// A YouTube channel id looks like UC + 22 characters; anything else is treated as a video.
bool LooksLikeChannelId(const QString &s)
{
	return s.size() == 24 && s.startsWith("UC") && !s.contains('/');
}

struct PlatformCard {
	int index = 0;
	const char *targetId = nullptr;
	const tandem::PlatformPreset *preset = nullptr;
	QGroupBox *box = nullptr;
	QCheckBox *stream = nullptr;
	QLineEdit *key = nullptr;
	QLineEdit *server = nullptr; // Kick only: per-account ingest URL
	QLabel *status = nullptr;
	QCheckBox *chat = nullptr;
	QLineEdit *channel = nullptr;
	QLineEdit *apiKey = nullptr; // YouTube only
	QWidget *chatFields = nullptr;
	QWidget *streamFields = nullptr;
};

class SimpleSetupWidget : public QWidget {
public:
	SimpleSetupWidget(SimpleSetupHost host, QWidget *parent) : QWidget(parent), host_(std::move(host))
	{
		auto layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);

		auto intro = new QLabel(Text("Simple.Intro"), this);
		intro->setWordWrap(true);
		layout->addWidget(intro);

		const char *platforms[] = {"twitch", "kick", "youtube"};
		for (int i = 0; i < 3; ++i) {
			cards_[i].index = i;
			cards_[i].targetId = kSimpleTargetIds[i];
			cards_[i].preset = &tandem::FindPlatformPreset(platforms[i]);
			BuildCard(cards_[i], layout);
		}

		// Encoder summary + quality
		auto encBox = new QGroupBox(Text("Simple.Encoding"), this);
		auto encLayout = new QFormLayout(encBox);
		encoder_ = new QLabel(encBox);
		encoder_->setWordWrap(true);
		quality_ = new QComboBox(encBox);
		quality_->addItem(Text("Simple.Quality.Auto"), 0);
		quality_->addItem(Text("Simple.Quality.Lower"), 1);
		quality_->addItem(Text("Simple.Quality.Higher"), 2);
		encLayout->addRow(encoder_);
		encLayout->addRow(Text("Simple.Quality"), quality_);
		upload_ = new QLabel(encBox);
		upload_->setWordWrap(true);
		encLayout->addRow(upload_);
		layout->addWidget(encBox);

		goLive_ = new QPushButton(this);
		goLive_->setMinimumHeight(goLive_->fontMetrics().height() * 3);
		QFont f = goLive_->font();
		f.setBold(true);
		f.setPointSizeF(f.pointSizeF() * 1.25);
		goLive_->setFont(f);
		QObject::connect(goLive_, &QPushButton::clicked, [this]() { ToggleLive(); });
		layout->addWidget(goLive_);

		liveSummary_ = new QLabel(this);
		liveSummary_->setWordWrap(true);
		layout->addWidget(liveSummary_);

		LoadFromConfig();
		ConnectSignals();

		timer_ = new QTimer(this);
		timer_->setInterval(500);
		QObject::connect(timer_, &QTimer::timeout, [this]() { RefreshStatus(); });
		timer_->start();
		RefreshStatus();
	}

private:
	void BuildCard(PlatformCard &c, QVBoxLayout *layout)
	{
		c.box = new QGroupBox(QString::fromUtf8(c.preset->label), this);
		auto form = new QFormLayout(c.box);

		auto top = new QHBoxLayout();
		c.stream = new QCheckBox(Text("Simple.StreamHere"), c.box);
		c.status = new QLabel(c.box);
		c.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		c.status->setWordWrap(true);
		top->addWidget(c.stream);
		top->addWidget(c.status, 1);
		form->addRow(top);

		// Key (and Kick's server) only show once the platform is ticked.
		c.streamFields = new QWidget(c.box);
		auto streamForm = new QFormLayout(c.streamFields);
		streamForm->setContentsMargins(0, 0, 0, 0);
		QHBoxLayout *keyRow = nullptr;
		c.key = MakeSecretEdit(c.streamFields, &keyRow);
		c.key->setPlaceholderText(Text("Simple.KeyPlaceholder"));
		c.key->setToolTip(QString::fromUtf8(c.preset->keyHelp));
		streamForm->addRow(Text("Simple.StreamKey"), keyRow);
		auto keyHelp = new QLabel(
			"<small>" +
				QString::fromStdString(tandem::KeyHelpHtml(*c.preset, tostdu8(Text("Tip.KeyLink")))) +
				"</small>",
			c.streamFields);
		keyHelp->setTextFormat(Qt::RichText);
		keyHelp->setOpenExternalLinks(true);
		keyHelp->setWordWrap(true);
		streamForm->addRow(keyHelp);
		if (strcmp(c.preset->id, "kick") == 0) {
			c.server = new QLineEdit(c.streamFields);
			c.server->setPlaceholderText(QString::fromUtf8(c.preset->server));
			c.server->setToolTip(Text("Simple.ServerTip"));
			streamForm->addRow(Text("Simple.Server"), c.server);
		}
		form->addRow(c.streamFields);

		c.chat = new QCheckBox(Text("Simple.ShowChat"), c.box);
		form->addRow(c.chat);
		c.chatFields = new QWidget(c.box);
		auto chatForm = new QFormLayout(c.chatFields);
		chatForm->setContentsMargins(0, 0, 0, 0);
		c.channel = new QLineEdit(c.chatFields);
		if (strcmp(c.preset->id, "youtube") == 0) {
			QHBoxLayout *apiRow = nullptr;
			c.apiKey = MakeSecretEdit(c.chatFields, &apiRow);
			c.apiKey->setPlaceholderText(Text("Simple.ApiKeyPlaceholder"));
			chatForm->addRow(Text("Chat.YouTube.ApiKey"), apiRow);
			c.channel->setPlaceholderText(Text("Simple.YouTubeVideoPlaceholder"));
			chatForm->addRow(Text("Simple.YouTubeVideo"), c.channel);
			auto link = new QLabel(Text("Chat.YouTube.ApiKeyHelp"), c.chatFields);
			link->setTextFormat(Qt::RichText);
			link->setOpenExternalLinks(true);
			link->setWordWrap(true);
			chatForm->addRow(link);
		} else {
			c.channel->setPlaceholderText(strcmp(c.preset->id, "twitch") == 0 ? Text("Chat.Twitch.ChannelHint")
											   : Text("Chat.Kick.ChannelHint"));
			chatForm->addRow(Text("Chat.Twitch.Channel"), c.channel);
		}
		form->addRow(c.chatFields);
		layout->addWidget(c.box);
	}

	static ChatPlatformConfig &ChatCfg(int index)
	{
		auto &chat = GlobalMultiOutputConfig().chat;
		return index == 0 ? chat.twitch : index == 1 ? chat.kick : chat.youtube;
	}

	void LoadFromConfig()
	{
		loading_ = true;
		auto &global = GlobalMultiOutputConfig();
		for (auto &c : cards_) {
			auto target = FindById(global.targets, std::string(c.targetId));
			c.stream->setChecked(target && target->syncStart);
			c.key->setText(target ? QString::fromUtf8(target->serviceParam.value("key", std::string()).c_str()) : "");
			if (c.server) {
				std::string server = target ? target->serviceParam.value("server", std::string()) : std::string();
				c.server->setText(server == c.preset->server ? QString() : QString::fromUtf8(server.c_str()));
			}
			auto &chat = ChatCfg(c.index);
			c.chat->setChecked(chat.enabled);
			if (c.apiKey) {
				c.apiKey->setText(QString::fromUtf8(global.chat.youtubeApiKey.c_str()));
				c.channel->setText(QString::fromUtf8((chat.channel.empty() ? chat.extra : chat.channel).c_str()));
			} else {
				c.channel->setText(QString::fromUtf8(chat.channel.c_str()));
			}
			c.chatFields->setVisible(c.chat->isChecked());
			c.streamFields->setVisible(c.stream->isChecked());
		}
		quality_->setCurrentIndex(std::clamp(global.simpleQuality, 0, 2));
		loading_ = false;
		seenRevision_ = MultiOutputConfigRevision();
		UpdateEncoderLabel();
	}

	void ConnectSignals()
	{
		for (auto &c : cards_) {
			auto save = [this]() { Apply(); };
			QObject::connect(c.stream, &QCheckBox::toggled, this, [this, &c](bool on) {
				c.streamFields->setVisible(on);
				if (on && !loading_ && c.key->text().isEmpty())
					c.key->setFocus();
				Apply();
			});
			QObject::connect(c.key, &QLineEdit::editingFinished, this, save);
			if (c.server)
				QObject::connect(c.server, &QLineEdit::editingFinished, this, save);
			QObject::connect(c.chat, &QCheckBox::toggled, this, [this, &c](bool on) {
				c.chatFields->setVisible(on);
				Apply();
			});
			QObject::connect(c.channel, &QLineEdit::editingFinished, this, save);
			if (c.apiKey)
				QObject::connect(c.apiKey, &QLineEdit::editingFinished, this, save);
		}
		QObject::connect(quality_, (void (QComboBox::*)(int)) & QComboBox::currentIndexChanged, this, [this](int) {
			Apply();
			UpdateEncoderLabel();
		});
	}

	bool AnySimpleBusy()
	{
		for (auto &c : cards_) {
			if (auto w = Widget(c); w && w->IsBusy())
				return true;
		}
		return false;
	}

	PushWidget *Widget(const PlatformCard &c)
	{
		if (!FindById(GlobalMultiOutputConfig().targets, std::string(c.targetId)))
			return nullptr;
		return host_.ensureWidget(c.targetId);
	}

	void ApplyEncoders()
	{
		auto &global = GlobalMultiOutputConfig();
		global.simpleQuality = quality_->currentIndex();
		if (AnySimpleBusy())
			return; // the shared encoder can't change while a Simple target is live
		auto choice = tandem::PickAutoEncoder(static_cast<tandem::Quality>(global.simpleQuality));
		if (choice.encoderId.empty())
			return;
		auto video = FindById(global.videoConfig, std::string(kSimpleVideoConfigId));
		if (!video) {
			video = std::make_shared<VideoEncoderConfig>();
			video->id = kSimpleVideoConfigId;
			global.videoConfig.push_back(video);
		}
		video->encoderId = choice.encoderId;
		video->encoderParams = choice.settings;
		video->fpsDenumerator = 1;
		video->outputScene.reset();
		video->resolution.reset();

		auto audio = FindById(global.audioConfig, std::string(kSimpleAudioConfigId));
		if (!audio) {
			audio = std::make_shared<AudioEncoderConfig>();
			audio->id = kSimpleAudioConfigId;
			global.audioConfig.push_back(audio);
		}
		audio->encoderId = tandem::AutoAudioEncoderId();
		audio->encoderParams = tandem::AutoAudioSettings();
		audio->mixerId = 0;
		audio->audioTracks.clear();
	}

	void Apply()
	{
		if (loading_)
			return;
		auto &global = GlobalMultiOutputConfig();
		ApplyEncoders();

		for (auto &c : cards_) {
			std::string key = tostdu8(c.key->text().trimmed());
			auto target = FindById(global.targets, std::string(c.targetId));
			bool want = c.stream->isChecked() || !key.empty();
			if (!target && want) {
				target = std::make_shared<OutputTargetConfig>();
				target->id = c.targetId;
				target->name = c.preset->label;
				target->platform = c.preset->id;
				target->protocol = "RTMP";
				target->serviceParam = nlohmann::json::object();
				target->outputParam = nlohmann::json::object();
				global.targets.push_back(target);
			}
			if (target) {
				auto w = Widget(c);
				bool busy = w && w->IsBusy();
				if (!busy) {
					std::string server = c.preset->server;
					if (c.server && !c.server->text().trimmed().isEmpty())
						server = tostdu8(c.server->text().trimmed());
					target->serviceParam["server"] = server;
					target->serviceParam["key"] = key;
					target->serviceParam["use_auth"] = false;
					target->videoConfig = kSimpleVideoConfigId;
					target->audioConfig = kSimpleAudioConfigId;
				}
				target->syncStart = c.stream->isChecked();
				target->syncStop = c.stream->isChecked();
				if (auto widget = host_.ensureWidget(c.targetId))
					widget->RefreshFromConfig();
			}

			auto &chat = ChatCfg(c.index);
			chat.enabled = c.chat->isChecked();
			QString channel = c.channel->text().trimmed();
			if (c.apiKey) {
				global.chat.youtubeApiKey = tostdu8(c.apiKey->text().trimmed());
				if (LooksLikeChannelId(channel)) {
					chat.channel.clear();
					chat.extra = tostdu8(channel);
				} else {
					chat.channel = tostdu8(channel);
					chat.extra.clear();
				}
			} else {
				chat.channel = tostdu8(channel);
			}
		}

		SaveMultiOutputConfig();
		seenRevision_ = MultiOutputConfigRevision();
		GetChatController().ApplyConfig();
		if (host_.changed)
			host_.changed();
		UpdateEncoderLabel();
		RefreshStatus();
	}

	void UpdateEncoderLabel()
	{
		auto choice = tandem::PickAutoEncoder(static_cast<tandem::Quality>(quality_->currentIndex()));
		if (choice.encoderId.empty()) {
			encoder_->setText(Text("Simple.NoEncoder"));
			return;
		}
		encoder_->setText(Text("Simple.EncoderSummary")
					  .arg(QString::fromUtf8(choice.label.c_str()))
					  .arg(QLocale().toString(choice.kbps)));
		kbps_ = choice.kbps + 160;
	}

	std::vector<PlatformCard *> Selected()
	{
		std::vector<PlatformCard *> out;
		for (auto &c : cards_) {
			if (c.stream->isChecked())
				out.push_back(&c);
		}
		return out;
	}

	bool AnyLive()
	{
		for (auto &c : cards_) {
			if (auto w = Widget(c); w && w->IsBusy())
				return true;
		}
		return false;
	}

	void ToggleLive()
	{
		Apply();
		if (AnyLive()) {
			for (auto &c : cards_) {
				if (auto w = Widget(c); w && w->IsBusy())
					w->StopStreaming();
			}
			if (startedChat_) {
				GetChatController().StopChat();
				startedChat_ = false;
			}
			RefreshStatus();
			return;
		}

		auto selected = Selected();
		if (selected.empty()) {
			QMessageBox::information(this, Text("Title"), Text("Simple.NothingSelected"));
			return;
		}
		QStringList missing;
		for (auto c : selected) {
			if (c->key->text().trimmed().isEmpty())
				missing << QString::fromUtf8(c->preset->label);
		}
		if (!missing.isEmpty()) {
			QMessageBox::warning(this, Text("Title"), Text("Simple.MissingKey").arg(missing.join(", ")));
			return;
		}
		for (auto c : selected) {
			if (auto w = Widget(*c))
				w->StartStreaming();
		}
		auto &chat = GlobalMultiOutputConfig().chat;
		bool wantChat = chat.twitch.enabled || chat.kick.enabled || chat.youtube.enabled;
		if (wantChat && !GetChatController().IsRunning()) {
			GetChatController().StartChat();
			startedChat_ = true;
		}
		RefreshStatus();
	}

	void RefreshStatus()
	{
		// Settings, Advanced mode or a profile switch changed the config: show it, so Apply
		// never writes stale card values back. Wait while the user is typing in a card.
		if (seenRevision_ != MultiOutputConfigRevision() && !isAncestorOf(QApplication::focusWidget()))
			LoadFromConfig();
		// Encoder plugins may still be loading when the dock is created.
		if (!encoderChecked_ && IsFrontendReady()) {
			encoderChecked_ = true;
			UpdateEncoderLabel();
		}
		bool live = false;
		QStringList liveOn;
		int selected = 0;
		for (auto &c : cards_) {
			auto w = Widget(c);
			bool busy = w && w->IsBusy();
			live |= busy;
			if (c.stream->isChecked())
				++selected;
			QString status;
			QString color = "gray";
			if (busy) {
				QString text = w->StatusText().section('\n', 0, 0);
				status = text.isEmpty() ? Text("Status.Connecting") : text;
				color = "#3a9a2f";
				liveOn << QString::fromUtf8(c.preset->label);
			} else if (w && !w->StatusText().isEmpty()) {
				status = w->StatusText().section('\n', 0, 0);
				color = "#e5534b";
			} else if (c.stream->isChecked()) {
				status = c.key->text().trimmed().isEmpty() ? Text("Simple.NeedsKey") : Text("Simple.Ready");
				color = c.key->text().trimmed().isEmpty() ? "#c9a227" : "gray";
			}
			c.status->setText(status);
			c.status->setStyleSheet(QString("color: %1;").arg(color));
			if (w)
				c.status->setToolTip(w->StatusText());
			// Keys and servers can't change mid-stream.
			c.key->setEnabled(!busy);
			if (c.server)
				c.server->setEnabled(!busy);
		}
		quality_->setEnabled(!live);

		goLive_->setText(Text(live ? "Simple.StopAll" : "Simple.GoLive"));
		goLive_->setStyleSheet(live ? "QPushButton { background-color: #b33a3a; color: white; }"
					    : "QPushButton { background-color: #2e7d32; color: white; }");
		liveSummary_->setText(live ? Text("Simple.LiveOn").arg(liveOn.join(", ")) : QString());

		double mbps = selected * kbps_ / 1000.0;
		upload_->setText(selected ? Text("Simple.Upload").arg(QString::number(mbps, 'f', 1)).arg(selected) : QString());
	}

	SimpleSetupHost host_;
	PlatformCard cards_[3];
	QLabel *encoder_ = nullptr;
	QComboBox *quality_ = nullptr;
	QLabel *upload_ = nullptr;
	QPushButton *goLive_ = nullptr;
	QLabel *liveSummary_ = nullptr;
	QTimer *timer_ = nullptr;
	bool loading_ = false;
	uint64_t seenRevision_ = 0;
	bool startedChat_ = false;
	bool encoderChecked_ = false;
	int kbps_ = 0;
};

} // namespace

QWidget *CreateSimpleSetupWidget(SimpleSetupHost host, QWidget *parent)
{
	return new SimpleSetupWidget(std::move(host), parent);
}
