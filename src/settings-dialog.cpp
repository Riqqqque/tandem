// SPDX-License-Identifier: GPL-2.0-or-later
#include "pch.h"

#include "settings-dialog.h"
#include "chat-controller.h"
#include "secrets.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QUrl>

namespace {

struct PlatformRow {
	QCheckBox *enabled = nullptr;
	QLineEdit *channel = nullptr;
	QLineEdit *extra = nullptr;
};

QLineEdit *SecretEdit(QWidget *parent, QHBoxLayout **outRow)
{
	auto edit = new QLineEdit(parent);
	edit->setEchoMode(QLineEdit::Password);
	auto show = new QPushButton(obs_module_text("Btn.Show"), parent);
	show->setCheckable(true);
	QObject::connect(show, &QPushButton::toggled, [edit, show](bool on) {
		edit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		show->setText(obs_module_text(on ? "Btn.Hide" : "Btn.Show"));
	});
	auto row = new QHBoxLayout();
	row->addWidget(edit, 1);
	row->addWidget(show);
	*outRow = row;
	return edit;
}

QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

void ShowTandemSettings(QWidget *parent, int tab)
{
	auto &global = GlobalMultiOutputConfig();
	auto &chat = global.chat;

	QDialog dlg(parent);
	dlg.setWindowTitle(Text("Settings.Title"));
	auto root = new QVBoxLayout(&dlg);
	auto tabs = new QTabWidget(&dlg);
	root->addWidget(tabs, 1);

	// Streaming
	auto streamPage = new QWidget(tabs);
	auto streamForm = new QFormLayout(streamPage);
	auto capacity = new QDoubleSpinBox(streamPage);
	capacity->setRange(0, 10000);
	capacity->setDecimals(1);
	capacity->setSuffix(" Mbps");
	capacity->setSpecialValueText(Text("Settings.Unknown"));
	capacity->setValue(global.uploadCapacityMbps);
	streamForm->addRow(Text("Settings.UploadCapacity"), capacity);
	auto capHelp = new QLabel(Text("Settings.UploadCapacityHelp"), streamPage);
	capHelp->setWordWrap(true);
	streamForm->addRow(capHelp);
	tabs->addTab(streamPage, Text("Settings.Tab.Streaming"));

	// Chat
	auto chatPage = new QWidget(tabs);
	auto chatLayout = new QVBoxLayout(chatPage);
	PlatformRow rows[3];
	{
		auto gp = new QGroupBox("Twitch", chatPage);
		auto form = new QFormLayout(gp);
		rows[0].enabled = new QCheckBox(Text("Chat.ReadChat"), gp);
		rows[0].channel = new QLineEdit(QString::fromUtf8(chat.twitch.channel.c_str()), gp);
		rows[0].channel->setPlaceholderText(Text("Chat.Twitch.ChannelHint"));
		form->addRow(rows[0].enabled);
		form->addRow(Text("Chat.Twitch.Channel"), rows[0].channel);
		chatLayout->addWidget(gp);
	}
	{
		auto gp = new QGroupBox("Kick", chatPage);
		auto form = new QFormLayout(gp);
		rows[1].enabled = new QCheckBox(Text("Chat.ReadChat"), gp);
		rows[1].channel = new QLineEdit(QString::fromUtf8(chat.kick.channel.c_str()), gp);
		rows[1].channel->setPlaceholderText(Text("Chat.Kick.ChannelHint"));
		rows[1].extra = new QLineEdit(QString::fromUtf8(chat.kick.extra.c_str()), gp);
		rows[1].extra->setPlaceholderText(Text("Chat.Kick.ChatroomHint"));
		form->addRow(rows[1].enabled);
		form->addRow(Text("Chat.Kick.Channel"), rows[1].channel);
		form->addRow(Text("Chat.Kick.Chatroom"), rows[1].extra);
		auto note = new QLabel(Text("Chat.Kick.Note"), gp);
		note->setWordWrap(true);
		form->addRow(note);
		chatLayout->addWidget(gp);
	}
	QLineEdit *apiKey = nullptr;
	{
		auto gp = new QGroupBox("YouTube", chatPage);
		auto form = new QFormLayout(gp);
		rows[2].enabled = new QCheckBox(Text("Chat.ReadChat"), gp);
		QHBoxLayout *keyRow = nullptr;
		apiKey = SecretEdit(gp, &keyRow);
		apiKey->setText(QString::fromUtf8(chat.youtubeApiKey.c_str()));
		rows[2].channel = new QLineEdit(QString::fromUtf8(chat.youtube.channel.c_str()), gp);
		rows[2].channel->setPlaceholderText(Text("Chat.YouTube.VideoHint"));
		rows[2].extra = new QLineEdit(QString::fromUtf8(chat.youtube.extra.c_str()), gp);
		rows[2].extra->setPlaceholderText(Text("Chat.YouTube.ChannelHint"));
		form->addRow(rows[2].enabled);
		form->addRow(Text("Chat.YouTube.ApiKey"), keyRow);
		form->addRow(Text("Chat.YouTube.Video"), rows[2].channel);
		form->addRow(Text("Chat.YouTube.Channel"), rows[2].extra);
		int64_t used = chat.youtubeQuotaDay == YouTubeQuotaDay() ? chat.youtubeQuotaUsed : 0;
		auto quota = new QLabel(Text("Chat.YouTube.Quota").arg(used).arg(10000), gp);
		quota->setWordWrap(true);
		form->addRow(quota);
		if (!tandem::SecretStoreAvailable()) {
			auto warn = new QLabel(Text("Settings.NoSecretStore"), gp);
			warn->setWordWrap(true);
			form->addRow(warn);
		}
		chatLayout->addWidget(gp);
	}
	rows[0].enabled->setChecked(chat.twitch.enabled);
	rows[1].enabled->setChecked(chat.kick.enabled);
	rows[2].enabled->setChecked(chat.youtube.enabled);

	auto general = new QGroupBox(Text("Settings.General"), chatPage);
	auto generalForm = new QFormLayout(general);
	auto startWithStream = new QCheckBox(Text("Chat.StartWithStream"), general);
	startWithStream->setChecked(chat.startWithStream);
	auto timestamps = new QCheckBox(Text("Chat.ShowTimestamps"), general);
	timestamps->setChecked(chat.showTimestamps);
	auto maxMessages = new QSpinBox(general);
	maxMessages->setRange(50, 5000);
	maxMessages->setValue(chat.maxMessages);
	generalForm->addRow(startWithStream);
	generalForm->addRow(timestamps);
	generalForm->addRow(Text("Chat.MaxMessages"), maxMessages);
	chatLayout->addWidget(general);
	chatLayout->addStretch(1);
	auto chatScroll = new QScrollArea(tabs);
	chatScroll->setWidgetResizable(true);
	chatScroll->setWidget(chatPage);
	tabs->addTab(chatScroll, Text("Settings.Tab.Chat"));

	// Overlay
	auto overlayPage = new QWidget(tabs);
	auto overlayForm = new QFormLayout(overlayPage);
	auto overlayOn = new QCheckBox(Text("Overlay.Enable"), overlayPage);
	overlayOn->setChecked(chat.overlayEnabled);
	auto port = new QSpinBox(overlayPage);
	port->setRange(1024, 65535);
	port->setValue(chat.overlayPort);
	auto url = new QLineEdit(overlayPage);
	url->setReadOnly(true);
	auto copy = new QPushButton(Text("Btn.Copy"), overlayPage);
	auto urlRow = new QHBoxLayout();
	urlRow->addWidget(url, 1);
	urlRow->addWidget(copy);
	auto overlayState = new QLabel(overlayPage);
	overlayState->setWordWrap(true);
	auto refreshOverlay = [&]() {
		url->setText(QString("http://127.0.0.1:%1/?max=20&fade=60").arg(port->value()));
		auto &ctl = GetChatController();
		if (!chat.overlayEnabled)
			overlayState->setText(Text("Overlay.Off"));
		else if (ctl.OverlayRunning())
			overlayState->setText(Text("Overlay.Running"));
		else
			overlayState->setText(Text("Overlay.Failed").arg(QString::fromUtf8(ctl.OverlayError().c_str())));
	};
	refreshOverlay();
	QObject::connect(port, &QSpinBox::valueChanged, [&](int) { refreshOverlay(); });
	QObject::connect(copy, &QPushButton::clicked, [&]() { QApplication::clipboard()->setText(url->text()); });
	auto overlayHelp = new QLabel(Text("Overlay.Help"), overlayPage);
	overlayHelp->setWordWrap(true);
	overlayHelp->setTextFormat(Qt::PlainText);
	overlayForm->addRow(overlayOn);
	overlayForm->addRow(Text("Overlay.Port"), port);
	overlayForm->addRow(Text("Overlay.Url"), urlRow);
	overlayForm->addRow(overlayState);
	overlayForm->addRow(overlayHelp);
	tabs->addTab(overlayPage, Text("Settings.Tab.Overlay"));

	tabs->setCurrentIndex(tab);

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	root->addWidget(buttons);
	dlg.resize(520, 640);

	if (dlg.exec() != QDialog::Accepted)
		return;

	auto trimmed = [](QLineEdit *e) { return tostdu8(e->text().trimmed()); };
	global.uploadCapacityMbps = capacity->value();
	ChatPlatformConfig *cfgs[3] = {&chat.twitch, &chat.kick, &chat.youtube};
	for (int i = 0; i < 3; ++i) {
		cfgs[i]->enabled = rows[i].enabled->isChecked();
		cfgs[i]->channel = trimmed(rows[i].channel);
		if (rows[i].extra)
			cfgs[i]->extra = trimmed(rows[i].extra);
	}
	chat.youtubeApiKey = trimmed(apiKey);
	chat.startWithStream = startWithStream->isChecked();
	chat.showTimestamps = timestamps->isChecked();
	chat.maxMessages = maxMessages->value();
	chat.overlayEnabled = overlayOn->isChecked();
	chat.overlayPort = port->value();
	SaveMultiOutputConfig();
	GetChatController().ApplyConfig();
}
