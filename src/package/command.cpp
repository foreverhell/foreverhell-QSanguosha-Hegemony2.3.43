/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#include "command.h"
#include "client.h"
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "roomthread.h"
#include "json.h"

class Enyuan : public TriggerSkill
{
public:
    Enyuan() : TriggerSkill("enyuan")
    {
        events << CardsMoveOneTime << Damaged;
    }

    virtual QStringList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data, ServerPlayer* &) const
    {
        if (!TriggerSkill::triggerable(player)) return QStringList();
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to && move.to == player && move.from && move.from->isAlive() && move.from != move.to && move.card_ids.size() >= 2 && (move.to_place == Player::PlaceHand)) {
                return QStringList(objectName());
            }
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from || damage.from->isDead() || damage.from == player) return QStringList();

            QStringList trigger_list;

            for (int i = 1; i <= damage.damage; i++)
                trigger_list << objectName();

            return trigger_list;
        }
        return QStringList();
    }

    virtual bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *target = (ServerPlayer *)move.from;
            if (!target || target->isDead()) return false;
            target->setFlags("EnyuanDrawTarget"); //for AI;
            bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(target));
            target->setFlags("-EnyuanDrawTarget");
            if (invoke) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                room->broadcastSkillInvoke(objectName(), 1, player);
                return true;
            }
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *target = damage.from;
            if (!target || target->isDead()) return false;
            if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                room->broadcastSkillInvoke(objectName(), 2, player);
                return true;
            }
        }
        return false;
    }

    virtual bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *) const
    {
        if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *target = (ServerPlayer *)move.from;
            if (!target || target->isDead()) return false;
            target->drawCards(1, objectName());
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *target = damage.from;
            if (!target || target->isDead()) return false;
            int num = (target->isFriendWith(player) ? 2 : 1);
            QList<int> result = room->askForExchange(target, objectName(), num, 0, "@enyuan-give:"+ player->objectName()+"::" + QString::number(num), "", ".|.|.|hand");
            if (result.isEmpty())
                room->loseHp(target);
            else {
                DummyCard dummy(result);
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), QString());
                reason.m_playerId = player->objectName();
                room->obtainCard(player, &dummy, reason, false);
            }
        }
        return false;
    }
};

class Xuanhuo : public TriggerSkill
{
public:
    Xuanhuo() : TriggerSkill("xuanhuo")
    {
        events << GeneralShown << Death << DFDebut;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    virtual void record(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player == NULL) return;
        if (triggerEvent == GeneralShown) {
            if (player->hasShownSkill(objectName())) {
                foreach(ServerPlayer *p, room->getOtherPlayers(player))
                    if (p->isFriendWith(player))
                        room->attachSkillToPlayer(p, "xuanhuoattach");
            } else {
                ServerPlayer *fazheng = room->findPlayerBySkillName(objectName());
                if (fazheng && fazheng->isAlive() && fazheng->hasShownSkill(objectName()))
                    room->attachSkillToPlayer(player, "xuanhuoattach");
            }
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player && player->hasSkill(objectName())) {
                foreach(ServerPlayer *p, room->getAlivePlayers()) {
                    room->detachSkillFromPlayer(p, "xuanhuoattach");
                }
            }
        } else if (triggerEvent == DFDebut) {
            ServerPlayer *fazheng = room->findPlayerBySkillName(objectName());
            if (fazheng && fazheng->isAlive() && fazheng->hasShownSkill(objectName()) && player != fazheng && !player->getAcquiredSkills().contains("xuanhuoattach")) {
                room->attachSkillToPlayer(player, "xuanhuoattach");
            }
        }
        return;
    }

    virtual QStringList triggerable(TriggerEvent , Room *, ServerPlayer *, QVariant &, ServerPlayer* &) const
    {
        return QStringList();
    }

};

XuanhuoAttachCard::XuanhuoAttachCard()
{
    target_fixed = true;
}

void XuanhuoAttachCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    ServerPlayer *shu = card_use.from;

    ServerPlayer *fazheng = room->findPlayerBySkillName("xuanhuo");
    if (!fazheng || fazheng->isDead() || !fazheng->isFriendWith(shu)) return;

    CardUseStruct new_use = card_use;
    new_use.to << fazheng;

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, shu, data);

    LogMessage log;
    log.type = "#InvokeOthersSkill";
    log.from = shu;
    log.to << fazheng;
    log.arg = "xuanhuo";
    room->sendLog(log);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, shu->objectName(), fazheng->objectName());
    room->broadcastSkillInvoke("xuanhuo", fazheng);

    room->notifySkillInvoked(fazheng, "xuanhuo");
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, shu->objectName(), fazheng->objectName(), "xuanhuo", QString());
    room->obtainCard(fazheng, this, reason, false);

    thread->trigger(CardUsed, room, shu, data);
    thread->trigger(CardFinished, room, shu, data);
}

void XuanhuoAttachCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QString all_skills = "wusheng+paoxiao+longdan+tieqi+liegong+kuanggu";
    QStringList skill_list;
    foreach (QString skill_name, all_skills.split("+")) {
        const Skill *skill = Sanguosha->getSkill(skill_name);
        if (skill && !source->getVisibleSkillList().contains(skill))
            skill_list << skill_name;
    }
    if (skill_list.isEmpty()) return;
    QString skill_name = room->askForChoice(source, "xuanhuo", skill_list.join("+"), QVariant(), "@xuanhuo-choose", all_skills);
    room->acquireSkill(source, skill_name, true, false);
    QStringList skills = source->tag["XuanhuoSkills"].toStringList();
    skills << skill_name;
    source->tag["XuanhuoSkills"] = QVariant::fromValue(skills);
}

class XuanhuoAttachVS : public ViewAsSkill
{
public:
    XuanhuoAttachVS() : ViewAsSkill("xuanhuoattach")
    {
        attached_lord_skill = true;
    }

    virtual bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return !to_select->isEquipped() && selected.length() < 2;
    }

    virtual bool isEnabledAtPlay(const Player *player) const
    {
       if (player->hasUsed("XuanhuoAttachCard")) return false;
       foreach (const Player *fazheng, player->getAliveSiblings()) {
           if (fazheng->hasShownSkill("xuanhuo") && player->isFriendWith(fazheng))
               return true;
       }
       return false;
    }

    virtual const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return NULL;

        XuanhuoAttachCard *rende_card = new XuanhuoAttachCard;
        rende_card->addSubcards(cards);
        return rende_card;
    }
};

class XuanhuoAttach : public TriggerSkill
{
public:
    XuanhuoAttach() : TriggerSkill("xuanhuoattach")
    {
        events << EventPhaseChanging;
        view_as_skill = new XuanhuoAttachVS;
        attached_lord_skill = true;
    }

    virtual void record(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive) {
            QStringList skills = player->tag["XuanhuoSkills"].toStringList();
            QStringList detachList;
            foreach(QString skill_name, skills)
                detachList.append("-" + skill_name + "!");
            room->handleAcquireDetachSkills(player, detachList, true);
            player->tag["XuanhuoSkills"] = QVariant();
        }
    }

    virtual TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *, QVariant &) const
    {
        return TriggerList();
    }
};

class Chaofeng : public TriggerSkill
{
public:
    Chaofeng() : TriggerSkill("chaofeng")
    {
        events << CardUsed << CardResponded;
    }

    virtual QStringList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer* &) const
    {
        if (TriggerSkill::triggerable(player) && player->getPhase() == Player::NotActive) {
            const Card *card = NULL;
            bool isHandcard = false;
            if (triggerEvent == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                card = use.card;
                isHandcard = use.m_isHandcard;
            } else if (triggerEvent == CardResponded) {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                card = resp.m_card;
                isHandcard = resp.m_isHandcard;
            }
            if (card && card->getTypeId() != Card::TypeSkill && card->isBlack() && isHandcard) {
                bool has_target = false, has_dying = false;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (player->inMyAttackRange(p))
                        has_target = true;
                    if (p->hasFlag("Global_Dying"))
                        has_dying = true;
                }
                if (has_target && !has_dying)
                    return QStringList(objectName());
            }
        }
        return QStringList();
    }

    virtual bool cost(TriggerEvent, Room *room, ServerPlayer *player, QVariant &, ServerPlayer *) const
    {
        QList<ServerPlayer *> players, mosts;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (player->inMyAttackRange(p))
                players << p;
        }
        if (players.isEmpty()) return false;
        int most = players.first()->getHp();
        foreach (ServerPlayer *p, players) {
            int h = p->getHp();
            if (h > most) {
                mosts.clear();
                most = h;
                mosts << p;
            } else if (most == h)
                mosts << p;
        }
        if (mosts.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, mosts, objectName(), "chaofeng-invoke", true, true);
        if (target) {
            player->tag["chaofeng-target"] = QVariant::fromValue(target);
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    virtual bool effect(TriggerEvent, Room *room, ServerPlayer *zhangxiu, QVariant &, ServerPlayer *) const
    {
        ServerPlayer *target = zhangxiu->tag["chaofeng-target"].value<ServerPlayer *>();
        zhangxiu->tag.remove("chaofeng-target");
        if (target)
            room->damage(DamageStruct(objectName(), zhangxiu, target, (target->hasShownAllGenerals() ? 1 : 2)));
        return false;
    }
};

CommandPackage::CommandPackage()
    : Package("command")
{
    General *fazheng = new General(this, "fazheng", "shu", 3);
    fazheng->addSkill(new Enyuan);
    fazheng->addSkill(new Xuanhuo);

    General *zhangxiu = new General(this, "zhangxiu", "qun");
    zhangxiu->addSkill(new Chaofeng);

    addMetaObject<XuanhuoAttachCard>();

    skills << new XuanhuoAttach;
}

ADD_PACKAGE(Command)
