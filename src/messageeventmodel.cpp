#include "messageeventmodel.h"

#include <connection.h>
#include <settings.h>
#include <user.h>

#include <events/redactionevent.h>
#include <events/roomavatarevent.h>
#include <events/roommemberevent.h>
#include <events/simplestateevents.h>

#include <QRegExp>
#include <QtCore/QDebug>
#include <QtQml>  // for qmlRegisterType()

static QString parseAvatarUrl(QUrl url) {
  return url.host() + "/" + url.path();
}

QHash<int, QByteArray> MessageEventModel::roleNames() const {
  QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
  roles[EventTypeRole] = "eventType";
  roles[MessageRole] = "message";
  roles[AboveEventTypeRole] = "aboveEventType";
  roles[EventIdRole] = "eventId";
  roles[TimeRole] = "time";
  roles[AboveTimeRole] = "aboveTime";
  roles[SectionRole] = "section";
  roles[AboveSectionRole] = "aboveSection";
  roles[AuthorRole] = "author";
  roles[AboveAuthorRole] = "aboveAuthor";
  roles[ContentRole] = "content";
  roles[ContentTypeRole] = "contentType";
  roles[HighlightRole] = "highlight";
  roles[ReadMarkerRole] = "readMarker";
  roles[SpecialMarksRole] = "marks";
  roles[LongOperationRole] = "progressInfo";
  roles[AnnotationRole] = "annotation";
  roles[EventResolvedTypeRole] = "eventResolvedType";
  roles[UserMarkerRole] = "userMarker";
  return roles;
}

MessageEventModel::MessageEventModel(QObject* parent)
    : QAbstractListModel(parent), m_currentRoom(nullptr) {
  using namespace QMatrixClient;
  qmlRegisterType<FileTransferInfo>();
  qRegisterMetaType<FileTransferInfo>();
  qmlRegisterUncreatableType<EventStatus>(
      "Spectral", 0, 1, "EventStatus", "EventStatus is not an creatable type");
}

MessageEventModel::~MessageEventModel() {}

void MessageEventModel::setRoom(SpectralRoom* room) {
  if (room == m_currentRoom) return;

  beginResetModel();
  if (m_currentRoom) {
    m_currentRoom->disconnect(this);
  }

  m_currentRoom = room;
  if (room) {
    lastReadEventId = room->readMarkerEventId();

    using namespace QMatrixClient;
    connect(m_currentRoom, &Room::aboutToAddNewMessages, this,
            [=](RoomEventsRange events) {
              beginInsertRows({}, timelineBaseIndex(),
                              timelineBaseIndex() + int(events.size()) - 1);
            });
    connect(m_currentRoom, &Room::aboutToAddHistoricalMessages, this,
            [=](RoomEventsRange events) {
              if (rowCount() > 0)
                rowBelowInserted = rowCount() - 1;  // See #312
              beginInsertRows({}, rowCount(),
                              rowCount() + int(events.size()) - 1);
            });
    connect(m_currentRoom, &Room::addedMessages, this,
            [=](int lowest, int biggest) {
              endInsertRows();
              if (biggest < m_currentRoom->maxTimelineIndex()) {
                auto rowBelowInserted = m_currentRoom->maxTimelineIndex() -
                                        biggest + timelineBaseIndex() - 1;
                refreshEventRoles(rowBelowInserted,
                                  {AboveEventTypeRole, AboveAuthorRole,
                                   AboveSectionRole, AboveTimeRole});
              }
              for (auto i = m_currentRoom->maxTimelineIndex() - biggest;
                   i <= m_currentRoom->maxTimelineIndex() - lowest; ++i)
                refreshLastUserEvents(i);
            });
    connect(m_currentRoom, &Room::pendingEventAboutToAdd, this,
            [this] { beginInsertRows({}, 0, 0); });
    connect(m_currentRoom, &Room::pendingEventAdded, this,
            &MessageEventModel::endInsertRows);
    connect(m_currentRoom, &Room::pendingEventAboutToMerge, this,
            [this](RoomEvent*, int i) {
              if (i == 0) return;  // No need to move anything, just refresh

              movingEvent = true;
              // Reverse i because row 0 is bottommost in the model
              const auto row = timelineBaseIndex() - i - 1;
              Q_ASSERT(beginMoveRows({}, row, row, {}, timelineBaseIndex()));
            });
    connect(m_currentRoom, &Room::pendingEventMerged, this, [this] {
      if (movingEvent) {
        endMoveRows();
        movingEvent = false;
      }
      refreshRow(timelineBaseIndex());  // Refresh the looks
      refreshLastUserEvents(0);
      if (m_currentRoom->timelineSize() > 1)  // Refresh above
        refreshEventRoles(timelineBaseIndex() + 1, {ReadMarkerRole});
      if (timelineBaseIndex() > 0)  // Refresh below, see #312
        refreshEventRoles(timelineBaseIndex() - 1,
                          {AboveEventTypeRole, AboveAuthorRole,
                           AboveSectionRole, AboveTimeRole});
    });
    connect(m_currentRoom, &Room::pendingEventChanged, this,
            &MessageEventModel::refreshRow);
    connect(m_currentRoom, &Room::pendingEventAboutToDiscard, this,
            [this](int i) { beginRemoveRows({}, i, i); });
    connect(m_currentRoom, &Room::pendingEventDiscarded, this,
            &MessageEventModel::endRemoveRows);
    connect(m_currentRoom, &Room::readMarkerMoved, this, [this] {
      refreshEventRoles(
          std::exchange(lastReadEventId, m_currentRoom->readMarkerEventId()),
          {ReadMarkerRole});
      refreshEventRoles(lastReadEventId, {ReadMarkerRole});
    });
    connect(m_currentRoom, &Room::replacedEvent, this,
            [this](const RoomEvent* newEvent) {
              refreshLastUserEvents(refreshEvent(newEvent->id()) -
                                    timelineBaseIndex());
            });
    connect(m_currentRoom, &Room::fileTransferProgress, this,
            &MessageEventModel::refreshEvent);
    connect(m_currentRoom, &Room::fileTransferCompleted, this,
            &MessageEventModel::refreshEvent);
    connect(m_currentRoom, &Room::fileTransferFailed, this,
            &MessageEventModel::refreshEvent);
    connect(m_currentRoom, &Room::fileTransferCancelled, this,
            &MessageEventModel::refreshEvent);
    connect(m_currentRoom, &Room::readMarkerForUserMoved, this,
            [=](User* user, QString fromEventId, QString toEventId) {
              refreshEventRoles(fromEventId, {UserMarkerRole});
              refreshEventRoles(toEventId, {UserMarkerRole});
            });
    qDebug() << "Connected to room" << room->id() << "as"
             << room->localUser()->id();
  } else
    lastReadEventId.clear();
  endResetModel();
}

int MessageEventModel::refreshEvent(const QString& eventId) {
  return refreshEventRoles(eventId);
}

void MessageEventModel::refreshRow(int row) { refreshEventRoles(row); }

int MessageEventModel::timelineBaseIndex() const {
  return m_currentRoom ? int(m_currentRoom->pendingEvents().size()) : 0;
}

void MessageEventModel::refreshEventRoles(int row, const QVector<int>& roles) {
  const auto idx = index(row);
  emit dataChanged(idx, idx, roles);
}

int MessageEventModel::refreshEventRoles(const QString& eventId,
                                         const QVector<int>& roles) {
  const auto it = m_currentRoom->findInTimeline(eventId);
  if (it == m_currentRoom->timelineEdge()) {
    qWarning() << "Trying to refresh inexistent event:" << eventId;
    return -1;
  }
  const auto row =
      it - m_currentRoom->messageEvents().rbegin() + timelineBaseIndex();
  refreshEventRoles(row, roles);
  return row;
}

inline bool hasValidTimestamp(const QMatrixClient::TimelineItem& ti) {
  return ti->timestamp().isValid();
}

QDateTime MessageEventModel::makeMessageTimestamp(
    const QMatrixClient::Room::rev_iter_t& baseIt) const {
  const auto& timeline = m_currentRoom->messageEvents();
  auto ts = baseIt->event()->timestamp();
  if (ts.isValid()) return ts;

  // The event is most likely redacted or just invalid.
  // Look for the nearest date around and slap zero time to it.
  using QMatrixClient::TimelineItem;
  auto rit = std::find_if(baseIt, timeline.rend(), hasValidTimestamp);
  if (rit != timeline.rend())
    return {rit->event()->timestamp().date(), {0, 0}, Qt::LocalTime};
  auto it = std::find_if(baseIt.base(), timeline.end(), hasValidTimestamp);
  if (it != timeline.end())
    return {it->event()->timestamp().date(), {0, 0}, Qt::LocalTime};

  // What kind of room is that?..
  qCritical() << "No valid timestamps in the room timeline!";
  return {};
}

QString MessageEventModel::renderDate(QDateTime timestamp) const {
  auto date = timestamp.toLocalTime().date();
  if (date == QDate::currentDate()) return tr("Today");
  if (date == QDate::currentDate().addDays(-1)) return tr("Yesterday");
  if (date == QDate::currentDate().addDays(-2))
    return tr("The day before yesterday");
  if (date > QDate::currentDate().addDays(-7)) return date.toString("dddd");
  return date.toString(Qt::DefaultLocaleShortDate);
}

bool MessageEventModel::isUserActivityNotable(
    const QMatrixClient::Room::rev_iter_t& baseIt) const {
  const auto& senderId = (*baseIt)->senderId();
  // TODO: Go up and down the timeline (limit to 100 events for
  // the sake of performance) and collect all messages of
  // this author; find out if there's anything besides joins, leaves
  // and redactions; if not, double-check whether the current event is
  // a part of a re-join without following redactions.
  using namespace QMatrixClient;
  bool joinFound = false, redactionsFound = false;
  // Find the nearest join of this user above, or a no-nonsense event.
  for (auto it = baseIt,
            limit = baseIt +
                    std::min(int(m_currentRoom->timelineEdge() - baseIt), 100);
       it != limit; ++it) {
    const auto& e = **it;
    if (e.senderId() != senderId) continue;
    if (e.isRedacted()) {
      redactionsFound = true;
      continue;
    }
    if (auto* me = it->viewAs<QMatrixClient::RoomMemberEvent>()) {
      if (me->isJoin()) {
        joinFound = true;
        break;
      }
      continue;
    }
    return true;  // Consider all other events notable
  }
  // Find the nearest leave of this user below, or a no-nonsense event
  bool leaveFound = false;
  for (auto it = baseIt.base() - 1,
            limit = baseIt.base() +
                    std::min(int(m_currentRoom->messageEvents().end() -
                                 baseIt.base()),
                             100);
       it != limit; ++it) {
    const auto& e = **it;
    if (e.senderId() != senderId) continue;
    if (e.isRedacted()) {
      redactionsFound = true;
      continue;
    }
    if (auto* me = it->viewAs<RoomMemberEvent>()) {
      if (me->isLeave() || me->membership() == MembershipType::Ban) {
        leaveFound = true;
        break;
      }
      continue;
    }
    return true;
  }
  // If we are here, it means that no notable events have been found in
  // the timeline vicinity, and probably redactions are there. Doesn't look
  // notable but let's give some benefit of doubt.
  if (redactionsFound) return false;  // Join + redactions or redactions + leave
  return !(joinFound && leaveFound);  // Join + (maybe profile changes) + leave
}

void MessageEventModel::refreshLastUserEvents(int baseTimelineRow) {
  if (!m_currentRoom || m_currentRoom->timelineSize() <= baseTimelineRow)
    return;
  const auto& timelineBottom = m_currentRoom->messageEvents().rbegin();
  const auto& lastSender = (*(timelineBottom + baseTimelineRow))->senderId();
  const auto limit = timelineBottom + std::min(baseTimelineRow + 100,
                                               m_currentRoom->timelineSize());
  for (auto it = timelineBottom + std::max(baseTimelineRow - 100, 0);
       it != limit; ++it) {
    if ((*it)->senderId() == lastSender) {
      auto idx = index(it - timelineBottom);
      emit dataChanged(idx, idx);
    }
  }
}

int MessageEventModel::rowCount(const QModelIndex& parent) const {
  if (!m_currentRoom || parent.isValid()) return 0;
  return m_currentRoom->timelineSize();
}

QVariant MessageEventModel::data(const QModelIndex& idx, int role) const {
  const auto row = idx.row();

  if (!m_currentRoom || row < 0 ||
      row >= int(m_currentRoom->pendingEvents().size()) +
                 m_currentRoom->timelineSize())
    return {};

  bool isPending = row < timelineBaseIndex();
  const auto timelineIt = m_currentRoom->messageEvents().crbegin() +
                          std::max(0, row - timelineBaseIndex());
  const auto pendingIt = m_currentRoom->pendingEvents().crbegin() +
                         std::min(row, timelineBaseIndex());
  const auto& evt = isPending ? **pendingIt : **timelineIt;

  using namespace QMatrixClient;
  if (role == Qt::DisplayRole) {
    if (evt.isRedacted()) {
      auto reason = evt.redactedBecause()->reason();
      if (reason.isEmpty()) return tr("Redacted");

      return tr("Redacted: %1").arg(evt.redactedBecause()->reason());
    }

    return visit(
        evt,
        [this](const RoomMessageEvent& e) {
          using namespace MessageEventContent;

          if (e.hasTextContent() && e.mimeType().name() != "text/plain") {
            static const QRegExp userPillRegExp(
                "<a href=\"https://matrix.to/#/@.*:.*\">(.*)</a>");
            QString formattedStr(
                static_cast<const TextContent*>(e.content())->body);
            formattedStr.replace(userPillRegExp,
                                 "<b class=\"user-pill\">\\1</b>");
            return formattedStr;
          }
          if (e.hasFileContent()) {
            auto fileCaption = e.content()->fileInfo()->originalName;
            if (fileCaption.isEmpty())
              fileCaption = m_currentRoom->prettyPrint(e.plainBody());
            if (fileCaption.isEmpty()) return tr("a file");
          }
          return m_currentRoom->prettyPrint(e.plainBody());
        },
        [this](const RoomMemberEvent& e) {
          // FIXME: Rewind to the name that was at the time of this event
          QString subjectName = m_currentRoom->roomMembername(e.userId());
          // The below code assumes senderName output in AuthorRole
          switch (e.membership()) {
            case MembershipType::Invite:
              if (e.repeatsState())
                return tr("reinvited %1 to the room").arg(subjectName);
              FALLTHROUGH;
            case MembershipType::Join: {
              if (e.repeatsState()) return tr("joined the room (repeated)");
              if (!e.prevContent() ||
                  e.membership() != e.prevContent()->membership) {
                return e.membership() == MembershipType::Invite
                           ? tr("invited %1 to the room").arg(subjectName)
                           : tr("joined the room");
              }
              QString text{};
              if (e.isRename()) {
                if (e.displayName().isEmpty())
                  text = tr("cleared their display name");
                else
                  text = tr("changed their display name to %1")
                             .arg(e.displayName());
              }
              if (e.isAvatarUpdate()) {
                if (!text.isEmpty()) text += " and ";
                if (e.avatarUrl().isEmpty())
                  text += tr("cleared the avatar");
                else
                  text += tr("updated the avatar");
              }
              return text;
            }
            case MembershipType::Leave:
              if (e.prevContent() &&
                  e.prevContent()->membership == MembershipType::Ban) {
                return (e.senderId() != e.userId())
                           ? tr("unbanned %1").arg(subjectName)
                           : tr("self-unbanned");
              }
              return (e.senderId() != e.userId())
                         ? tr("has kicked %1 from the room").arg(subjectName)
                         : tr("left the room");
            case MembershipType::Ban:
              return (e.senderId() != e.userId())
                         ? tr("banned %1 from the room").arg(subjectName)
                         : tr("self-banned from the room");
            case MembershipType::Knock:
              return tr("knocked");
            default:;
          }
          return tr("made something unknown");
        },
        [](const RoomAliasesEvent& e) {
          return tr("set aliases to: %1").arg(e.aliases().join(", "));
        },
        [](const RoomCanonicalAliasEvent& e) {
          return (e.alias().isEmpty())
                     ? tr("cleared the room main alias")
                     : tr("set the room main alias to: %1").arg(e.alias());
        },
        [](const RoomNameEvent& e) {
          return (e.name().isEmpty())
                     ? tr("cleared the room name")
                     : tr("set the room name to: %1").arg(e.name());
        },
        [](const RoomTopicEvent& e) {
          return (e.topic().isEmpty())
                     ? tr("cleared the topic")
                     : tr("set the topic to: %1").arg(e.topic());
        },
        [](const RoomAvatarEvent&) { return tr("changed the room avatar"); },
        [](const EncryptionEvent&) {
          return tr("activated End-to-End Encryption");
        },
        tr("Unknown Event"));
  }

  if (role == MessageRole) {
    static const QRegExp rmReplyRegExp("^> <@.*:.*> .*\n\n(.*)");
    return evt.contentJson().value("body").toString().replace(rmReplyRegExp,
                                                              "\\1");
  }

  if (role == Qt::ToolTipRole) {
    return evt.originalJson();
  }

  if (role == EventTypeRole) {
    if (auto e = eventCast<const RoomMessageEvent>(&evt)) {
      switch (e->msgtype()) {
        case MessageEventType::Emote:
          return "emote";
        case MessageEventType::Notice:
          return "notice";
        case MessageEventType::Image:
          return "image";
        case MessageEventType::Audio:
          return "audio";
        default:
          return e->hasFileContent() ? "file" : "message";
      }
    }
    if (evt.isStateEvent()) return "state";

    return "other";
  }

  if (role == EventResolvedTypeRole)
    return EventTypeRegistry::getMatrixType(evt.type());

  if (role == AuthorRole) {
    // FIXME: It shouldn't be User, it should be its state "as of event"
    return QVariant::fromValue(isPending ? m_currentRoom->localUser()
                                         : m_currentRoom->user(evt.senderId()));
  }

  if (role == ContentTypeRole) {
    if (auto e = eventCast<const RoomMessageEvent>(&evt)) {
      const auto& contentType = e->mimeType().name();
      return contentType == "text/plain" ? QStringLiteral("text/html")
                                         : contentType;
    }
    return QStringLiteral("text/plain");
  }

  if (role == ContentRole) {
    if (evt.isRedacted()) {
      auto reason = evt.redactedBecause()->reason();
      return (reason.isEmpty())
                 ? tr("Redacted")
                 : tr("Redacted: %1").arg(evt.redactedBecause()->reason());
    }

    if (auto e = eventCast<const RoomMessageEvent>(&evt)) {
      // Cannot use e.contentJson() here because some
      // EventContent classes inject values into the copy of the
      // content JSON stored in EventContent::Base
      return e->hasFileContent()
                 ? QVariant::fromValue(e->content()->originalJson)
                 : QVariant();
    };
  }

  if (role == HighlightRole) return m_currentRoom->isEventHighlighted(&evt);

  if (role == ReadMarkerRole) return evt.id() == lastReadEventId;

  if (role == SpecialMarksRole) {
    if (isPending) return pendingIt->deliveryStatus();

    if (is<RedactionEvent>(evt)) return EventStatus::Hidden;
    auto* memberEvent = timelineIt->viewAs<RoomMemberEvent>();
    if (memberEvent) {
      if ((memberEvent->isJoin() || memberEvent->isLeave()))
        return EventStatus::Hidden;
    }
    if (memberEvent || evt.isRedacted()) {
      if (evt.senderId() == m_currentRoom->localUser()->id()) {
        //            QElapsedTimer et; et.start();
        auto hide = !isUserActivityNotable(timelineIt);
        //            qDebug() << "Checked user activity for" << evt.id() <<
        //            "in" << et;
        if (hide) return EventStatus::Hidden;
      }
    }
    if (evt.isRedacted()) return EventStatus::Redacted;

    if (evt.isStateEvent() &&
        static_cast<const StateEventBase&>(evt).repeatsState())
      return EventStatus::Hidden;

    return EventStatus::Normal;
  }

  if (role == EventIdRole)
    return !evt.id().isEmpty() ? evt.id() : evt.transactionId();

  if (role == LongOperationRole) {
    if (auto e = eventCast<const RoomMessageEvent>(&evt))
      if (e->hasFileContent())
        return QVariant::fromValue(m_currentRoom->fileTransferInfo(e->id()));
  }

  if (role == AnnotationRole)
    if (isPending) return pendingIt->annotation();

  if (role == TimeRole || role == SectionRole) {
    auto ts =
        isPending ? pendingIt->lastUpdated() : makeMessageTimestamp(timelineIt);
    return role == TimeRole ? QVariant(ts) : renderDate(ts);
  }

  if (role == UserMarkerRole) {
    QVariantList variantList;
    for (User* user : m_currentRoom->usersAtEventId(evt.id())) {
      if (user == m_currentRoom->localUser()) continue;
      variantList.append(QVariant::fromValue(user));
    }
    return variantList;
  }

  if (role == AboveEventTypeRole || role == AboveSectionRole ||
      role == AboveAuthorRole || role == AboveTimeRole)
    for (auto r = row + 1; r < rowCount(); ++r) {
      auto i = index(r);
      if (data(i, SpecialMarksRole) != EventStatus::Hidden) switch (role) {
          case AboveEventTypeRole:
            return data(i, EventTypeRole);
          case AboveSectionRole:
            return data(i, SectionRole);
          case AboveAuthorRole:
            return data(i, AuthorRole);
          case AboveTimeRole:
            return data(i, TimeRole);
        }
    }

  return {};
}
