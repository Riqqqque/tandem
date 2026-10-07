// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - unified chat dock. Drains the chat hub on a timer so chat floods are
// rendered in batches instead of one repaint per message.
#include "pch.h"

#include "chat-dock.h"
#include "chat-controller.h"
#include "settings-dialog.h"

#include <QAbstractListModel>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QThreadPool>

#include "chat/emotes.h"
#include "chat/net.h"

#include <deque>

using namespace tandem::chat;

#include <atomic>
#include <functional>

namespace {

const char *PlatformColor(Platform p)
{
	switch (p) {
	case Platform::Twitch:
		return "#8d5bd9";
	case Platform::Kick:
		return "#3a9a2f";
	case Platform::YouTube:
		return "#d03a3a";
	}
	return "#777777";
}

QString FallbackColor(const std::string &name)
{
	static const char *palette[] = {"#e0703a", "#3a8fe0", "#c0469b", "#2fa58f", "#c9a227", "#7f6bd6", "#d4545e", "#4aa84a"};
	uint32_t h = 2166136261u;
	for (unsigned char c : name)
		h = (h ^ c) * 16777619u;
	return palette[h % (sizeof(palette) / sizeof(*palette))];
}

// Downloads emote images off the UI thread and keeps them scaled to the chat line height.
// OBS's Qt has no TLS, so the download goes through the chat core's libcurl helper.
class EmoteImages : public QObject {
public:
	static EmoteImages &Instance()
	{
		static EmoteImages instance;
		return instance;
	}

	// Returns the image if it is ready; otherwise starts a download and returns a null image.
	QImage Get(const QString &url, int height)
	{
		auto it = cache_.find(url);
		if (it != cache_.end())
			return it.value();
		if (pending_.contains(url) || failed_.contains(url) || !IsTrustedEmoteUrl(tostdu8(url)))
			return {};
		if (cache_.size() > 3000)
			cache_.clear();
		pending_.insert(url);
		QPointer<QObject> self(this);
		std::string u = tostdu8(url);
		QThreadPool::globalInstance()->start([this, self, url, u, height]() {
			HttpResponse r = HttpGet(u, {"Accept: image/webp,image/png,image/gif,image/*"}, 15000, &stopping_);
			QImage img;
			if (r.error.empty() && r.status == 200)
				img = QImage::fromData(QByteArray(r.body.data(), (qsizetype)r.body.size()));
			if (!img.isNull() && img.height() != height)
				img = img.scaledToHeight(height, Qt::SmoothTransformation);
			QMetaObject::invokeMethod(
				this,
				[this, self, url, img]() {
					if (!self)
						return;
					pending_.remove(url);
					if (img.isNull())
						failed_.insert(url);
					else
						cache_.insert(url, img);
					for (auto &cb : listeners_)
						cb();
				},
				Qt::QueuedConnection);
		});
		return {};
	}

	void AddListener(std::function<void()> cb) { listeners_.push_back(std::move(cb)); }
	void Shutdown() { stopping_ = true; }

private:
	QHash<QString, QImage> cache_;
	QSet<QString> pending_;
	QSet<QString> failed_;
	std::vector<std::function<void()>> listeners_;
	std::atomic<bool> stopping_{false};
};

// A text document that resolves <img src="https://..."> through EmoteImages.
class EmoteDocument : public QTextDocument {
public:
	explicit EmoteDocument(int emoteHeight) : emoteHeight_(emoteHeight) {}

protected:
	QVariant loadResource(int type, const QUrl &name) override
	{
		if (type == QTextDocument::ImageResource && name.scheme() == "https") {
			QImage img = EmoteImages::Instance().Get(name.toString(), emoteHeight_);
			if (!img.isNull())
				return img;
			// Placeholder until the download finishes, so the line height doesn't jump.
			QImage blank(emoteHeight_, emoteHeight_, QImage::Format_ARGB32_Premultiplied);
			blank.fill(Qt::transparent);
			return blank;
		}
		return QTextDocument::loadResource(type, name);
	}

private:
	int emoteHeight_;
};

struct Entry {
	ChatMessage msg;
	QString html;
};

class ChatModel : public QAbstractListModel {
public:
	enum Roles { PlatformRole = Qt::UserRole + 1, HtmlRole };

	int rowCount(const QModelIndex &parent = QModelIndex()) const override
	{
		return parent.isValid() ? 0 : (int)entries_.size();
	}

	QVariant data(const QModelIndex &index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= (int)entries_.size())
			return {};
		const auto &e = entries_[(size_t)index.row()];
		switch (role) {
		case Qt::DisplayRole:
			return QString::fromUtf8("[%1] %2: %3")
				.arg(PlatformName(e.msg.platform), QString::fromUtf8(e.msg.author.c_str()),
				     QString::fromUtf8(e.msg.text.c_str()));
		case PlatformRole:
			return static_cast<int>(e.msg.platform);
		case HtmlRole:
			return e.html;
		default:
			return {};
		}
	}

	void SetShowTimestamps(bool on)
	{
		if (showTimestamps_ == on)
			return;
		showTimestamps_ = on;
		for (auto &e : entries_)
			e.html = BuildHtml(e.msg);
		if (!entries_.empty())
			emit dataChanged(index(0), index((int)entries_.size() - 1));
	}

	void Append(std::vector<ChatMessage> &&batch, size_t cap)
	{
		if (batch.empty())
			return;
		// Only the newest `cap` messages matter.
		if (batch.size() > cap)
			batch.erase(batch.begin(), batch.end() - (ptrdiff_t)cap);
		size_t overflow = entries_.size() + batch.size() > cap ? entries_.size() + batch.size() - cap : 0;
		if (overflow > 0) {
			overflow = std::min(overflow, entries_.size());
			if (overflow > 0) {
				beginRemoveRows(QModelIndex(), 0, (int)overflow - 1);
				entries_.erase(entries_.begin(), entries_.begin() + (ptrdiff_t)overflow);
				endRemoveRows();
			}
		}
		int first = (int)entries_.size();
		beginInsertRows(QModelIndex(), first, first + (int)batch.size() - 1);
		for (auto &m : batch) {
			Entry e;
			e.html = BuildHtml(m);
			e.msg = std::move(m);
			entries_.push_back(std::move(e));
		}
		endInsertRows();
	}

	void RemoveIf(const std::function<bool(const ChatMessage &)> &pred)
	{
		for (int row = (int)entries_.size() - 1; row >= 0; --row) {
			if (pred(entries_[(size_t)row].msg)) {
				beginRemoveRows(QModelIndex(), row, row);
				entries_.erase(entries_.begin() + row);
				endRemoveRows();
			}
		}
	}

	void RefreshAll()
	{
		if (!entries_.empty())
			emit dataChanged(index(0), index((int)entries_.size() - 1));
	}

	void Clear()
	{
		beginResetModel();
		entries_.clear();
		endResetModel();
	}

	void Trim(size_t cap)
	{
		if (entries_.size() <= cap)
			return;
		size_t n = entries_.size() - cap;
		beginRemoveRows(QModelIndex(), 0, (int)n - 1);
		entries_.erase(entries_.begin(), entries_.begin() + (ptrdiff_t)n);
		endRemoveRows();
	}

private:
	QString BuildHtml(const ChatMessage &m) const
	{
		QString html;
		if (showTimestamps_ && m.timestamp_ms > 0) {
			auto t = QDateTime::fromMSecsSinceEpoch(m.timestamp_ms).toString("HH:mm");
			html += QString("<span style=\"color:gray\">%1</span> ").arg(t);
		}
		html += QString("<span style=\"background-color:%1;color:#ffffff;\">&nbsp;%2&nbsp;</span> ")
				.arg(PlatformColor(m.platform), PlatformName(m.platform));
		for (auto &b : m.badges) {
			if (b == "broadcaster" || b == "owner")
				html += "<b title=\"owner\">&#9733;</b>";
			else if (b == "moderator")
				html += "<b title=\"moderator\">&#9876;</b>";
		}
		QString color = m.color.empty() ? FallbackColor(m.author) : QString::fromUtf8(m.color.c_str());
		html += QString("<b style=\"color:%1\">%2</b>: %3")
				.arg(color.toHtmlEscaped(), QString::fromUtf8(m.author.c_str()).toHtmlEscaped(), TextHtml(m));
		return html;
	}

	// Message text as HTML: escaped text runs, and <img> for emotes on trusted CDNs.
	static QString TextHtml(const ChatMessage &m)
	{
		QString out;
		size_t pos = 0;
		auto text = [&](size_t from, size_t to) {
			out += QString::fromUtf8(m.text.data() + from, (qsizetype)(to - from)).toHtmlEscaped();
		};
		for (const auto &e : m.emotes) {
			if (e.begin < pos || e.end > m.text.size() || !IsTrustedEmoteUrl(e.url))
				continue;
			text(pos, e.begin);
			out += QString("<img src=\"%1\" title=\"%2\" style=\"vertical-align: middle\">")
				       .arg(QString::fromUtf8(e.url.c_str()).toHtmlEscaped(),
					    QString::fromUtf8(e.name.c_str()).toHtmlEscaped());
			pos = e.end;
		}
		text(pos, m.text.size());
		return out;
	}

	std::deque<Entry> entries_;
	bool showTimestamps_ = false;
};

class PlatformFilter : public QSortFilterProxyModel {
public:
	bool visible[kPlatformCount] = {true, true, true};

	void Refresh()
	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
		beginFilterChange();
		endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
		invalidateRowsFilter();
#endif
	}

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override
	{
		auto idx = sourceModel()->index(row, 0, parent);
		int p = sourceModel()->data(idx, ChatModel::PlatformRole).toInt();
		return p >= 0 && p < kPlatformCount && visible[p];
	}
};

class ChatDelegate : public QStyledItemDelegate {
public:
	explicit ChatDelegate(QListView *view) : QStyledItemDelegate(view), view_(view) {}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		opt.text.clear();
		opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		EmoteDocument doc(EmoteHeight());
		Setup(doc, index, option.rect.width());
		painter->save();
		painter->translate(option.rect.topLeft() + QPoint(kPad, kPad / 2));
		QAbstractTextDocumentLayout::PaintContext ctx;
		ctx.palette = option.palette;
		ctx.palette.setColor(QPalette::Text, option.palette.color(QPalette::Text));
		doc.documentLayout()->draw(painter, ctx);
		painter->restore();
	}

	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		int width = view_->viewport()->width();
		if (width <= 0)
			width = option.rect.width();
		EmoteDocument doc(EmoteHeight());
		Setup(doc, index, width);
		return QSize(width, (int)std::ceil(doc.size().height()) + kPad);
	}

private:
	static constexpr int kPad = 4;

	int EmoteHeight() const { return (int)(QFontMetrics(view_->font()).height() * 1.6); }

	void Setup(QTextDocument &doc, const QModelIndex &index, int width) const
	{
		doc.setDocumentMargin(0);
		doc.setDefaultFont(view_->font());
		doc.setHtml(index.data(ChatModel::HtmlRole).toString());
		doc.setTextWidth((std::max)(50, width - 2 * kPad));
	}

	QListView *view_;
};

class ChatDock : public QWidget {
public:
	ChatDock()
	{
		auto layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);

		// Status row, one pill per platform. Each doubles as the show/hide filter.
		auto statusRow = new QHBoxLayout();
		for (int i = 0; i < kPlatformCount; ++i) {
			auto box = new QCheckBox(this);
			box->setChecked(true);
			box->setToolTip(obs_module_text("Chat.FilterTip"));
			filters_[i] = box;
			statusRow->addWidget(box);
			QObject::connect(box, &QCheckBox::toggled, [this, i](bool on) {
				filter_.visible[i] = on;
				filter_.Refresh();
				auto &chat = GlobalMultiOutputConfig().chat;
				(i == 0 ? chat.twitch : i == 1 ? chat.kick : chat.youtube).visible = on;
				SaveMultiOutputConfig();
				ScrollToBottom();
			});
		}
		statusRow->addStretch(1);
		layout->addLayout(statusRow);

		view_ = new QListView(this);
		view_->setUniformItemSizes(false);
		view_->setWordWrap(true);
		view_->setResizeMode(QListView::Adjust);
		view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
		view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
		filter_.setSourceModel(&model_);
		view_->setModel(&filter_);
		view_->setItemDelegate(new ChatDelegate(view_));
		layout->addWidget(view_, 1);

		auto buttons = new QHBoxLayout();
		toggle_ = new QPushButton(this);
		QObject::connect(toggle_, &QPushButton::clicked, [this]() {
			auto &ctl = GetChatController();
			if (ctl.IsRunning())
				ctl.StopChat();
			else
				ctl.StartChat();
			UpdateControls();
		});
		buttons->addWidget(toggle_);
		auto clear = new QPushButton(obs_module_text("Chat.Clear"), this);
		QObject::connect(clear, &QPushButton::clicked, [this]() {
			model_.Clear();
			GetChatController().Hub().ClearHistory();
		});
		buttons->addWidget(clear);
		auto settings = new QPushButton(obs_module_text("Btn.Settings"), this);
		QObject::connect(settings, &QPushButton::clicked, [this]() {
			ShowTandemSettings(this, 1);
			ApplyViewConfig();
		});
		buttons->addWidget(settings);
		layout->addLayout(buttons);

		// Re-layout once emote images arrive (batched, so a burst of downloads costs one pass).
		QPointer<QWidget> self(this);
		EmoteImages::Instance().AddListener([this, self]() {
			if (!self || emoteRefreshPending_)
				return;
			emoteRefreshPending_ = true;
			QTimer::singleShot(150, this, [this]() {
				emoteRefreshPending_ = false;
				bool stick = AtBottom();
				model_.RefreshAll();
				view_->doItemsLayout();
				if (stick)
					ScrollToBottom();
			});
		});

		subscription_ = GetChatController().Hub().Subscribe();
		timer_ = new QTimer(this);
		timer_->setInterval(100);
		QObject::connect(timer_, &QTimer::timeout, [this]() { Pump(); });
		timer_->start();

		ApplyViewConfig();
		UpdateControls();
	}

	~ChatDock() override
	{
		EmoteImages::Instance().Shutdown();
		GetChatController().Hub().Unsubscribe(subscription_);
	}

private:
	void ApplyViewConfig()
	{
		auto &chat = GlobalMultiOutputConfig().chat;
		bool vis[kPlatformCount] = {chat.twitch.visible, chat.kick.visible, chat.youtube.visible};
		for (int i = 0; i < kPlatformCount; ++i) {
			QSignalBlocker block(filters_[i]);
			filters_[i]->setChecked(vis[i]);
			filter_.visible[i] = vis[i];
		}
		filter_.Refresh();
		model_.SetShowTimestamps(chat.showTimestamps);
		model_.Trim((size_t)chat.maxMessages);
		UpdateControls();
	}

	void UpdateControls()
	{
		auto &ctl = GetChatController();
		toggle_->setText(obs_module_text(ctl.IsRunning() ? "Chat.Stop" : "Chat.Start"));
		auto &chat = GlobalMultiOutputConfig().chat;
		const ChatPlatformConfig *cfgs[kPlatformCount] = {&chat.twitch, &chat.kick, &chat.youtube};
		for (int i = 0; i < kPlatformCount; ++i) {
			auto p = static_cast<Platform>(i);
			auto st = ctl.Hub().Status(p);
			QString state;
			QString color = "gray";
			if (!cfgs[i]->enabled) {
				state = obs_module_text("Chat.State.Off");
			} else if (!ctl.IsRunning()) {
				state = obs_module_text("Chat.State.Idle");
			} else {
				switch (st.state) {
				case ProviderState::Connected:
					state = obs_module_text("Chat.State.Connected");
					color = "#3a9a2f";
					break;
				case ProviderState::Connecting:
					state = obs_module_text("Chat.State.Connecting");
					color = "#c9a227";
					break;
				case ProviderState::Reconnecting:
					state = obs_module_text("Chat.State.Reconnecting");
					color = "#c9a227";
					break;
				case ProviderState::Error:
					state = obs_module_text("Chat.State.Error");
					color = "#e5534b";
					break;
				case ProviderState::Stopped:
					state = obs_module_text("Chat.State.Idle");
					break;
				}
			}
			filters_[i]->setText(QString("%1: %2").arg(PlatformName(p), state));
			filters_[i]->setStyleSheet(QString("QCheckBox { color: %1; }").arg(color));
			QString tip = QString::fromUtf8(obs_module_text("Chat.FilterTip"));
			if (!st.detail.empty())
				tip = QString::fromUtf8(st.detail.c_str()) + "\n\n" + tip;
			filters_[i]->setToolTip(tip);
		}
	}

	bool AtBottom() const
	{
		auto bar = view_->verticalScrollBar();
		return bar->value() >= bar->maximum() - 4;
	}

	void ScrollToBottom() { view_->scrollToBottom(); }

	void Pump()
	{
		std::vector<ChatEvent> events;
		GetChatController().Hub().Drain(subscription_, events, 5000);
		bool statusChanged = false;
		bool stick = AtBottom();
		std::vector<ChatMessage> batch;
		auto flush = [&]() {
			if (!batch.empty()) {
				model_.Append(std::move(batch), (size_t)GlobalMultiOutputConfig().chat.maxMessages);
				batch.clear();
			}
		};
		for (auto &ev : events) {
			switch (ev.kind) {
			case ChatEvent::Kind::Message:
				batch.push_back(std::move(ev.message));
				break;
			case ChatEvent::Kind::DeleteMessage:
				flush();
				model_.RemoveIf([&](const ChatMessage &m) {
					return m.platform == ev.platform && m.id == ev.target_id;
				});
				break;
			case ChatEvent::Kind::ClearUser:
				flush();
				model_.RemoveIf([&](const ChatMessage &m) {
					return m.platform == ev.platform && !ev.target_id.empty() && m.author_id == ev.target_id;
				});
				break;
			case ChatEvent::Kind::ClearAll:
				flush();
				model_.RemoveIf([&](const ChatMessage &m) { return m.platform == ev.platform; });
				break;
			case ChatEvent::Kind::Status:
			case ChatEvent::Kind::Quota:
				statusChanged = true;
				break;
			case ChatEvent::Kind::ChannelInfo:
				break;
			}
		}
		flush();
		if (!events.empty() && stick)
			ScrollToBottom();
		if (statusChanged || ++ticks_ % 10 == 0)
			UpdateControls();
	}

	ChatModel model_;
	PlatformFilter filter_;
	QListView *view_ = nullptr;
	QCheckBox *filters_[kPlatformCount] = {};
	QPushButton *toggle_ = nullptr;
	QTimer *timer_ = nullptr;
	int subscription_ = 0;
	unsigned ticks_ = 0;
	bool emoteRefreshPending_ = false;
};

} // namespace

QWidget *CreateChatDock()
{
	auto dock = new ChatDock();
	dock->setObjectName("tandem-chat-dock");
	return dock;
}
