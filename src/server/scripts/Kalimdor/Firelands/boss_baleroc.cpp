#include "ScriptPCH.h"
#include "firelands.h"

enum ScriptTexts
{
    SAY_AGGRO       = 0,
    SAY_DEATH       = 1,
    SAY_SHARDS      = 2,
    SAY_DECIMATION  = 3,
    SAY_KILL        = 4,
    SAY_BERSERK     = 5,
    SAY_INFERNO     = 6,
};

enum Spells
{
    SPELL_BLAZE_OF_GLORY            = 99252,
    SPELL_INCENDIARY_SOUL           = 99369,
    SPELL_SHARDS_OF_TORMENT_AOE     = 99259,
    SPELL_SHARDS_OF_TORMENT         = 99260,
    SPELL_TORMENT_VISUAL            = 99254,
    SPELL_TORMENT                   = 99255,
    SPELL_TORMENT_DMG               = 99256,
    SPELL_TORMENTED                 = 99257,
    SPELL_TORMENTED_25H             = 99404,
    SPELL_TORMENTED_AOE             = 99489,
    SPELL_WAVE_OF_TORMENT           = 99261,
    SPELL_VITAL_SPARK               = 99262,
    SPELL_VITAL_FLAME               = 99263,
    SPELL_DECIMATION_BLADE          = 99352,
    SPELL_DECIMATION_BLADE_25       = 99405,
    SPELL_DECIMATION_BLADE_DMG      = 99353,
    SPELL_INFERNO_BLADE             = 99350,
    SPELL_INFERNO_BLADE_DMG         = 99351,
    SPELL_BERSERK                   = 26662,
    SPELL_FINAL_COUNTDOWN           = 99515,
    SPELL_FINAL_COUNTDOWN_AURA      = 99516,
    SPELL_FINAL_COUNTDOWN_SCRIPT    = 99517,
    SPELL_FINAL_COUNTDOWN_DMG       = 99518,
    SPELL_FINAL_COUNTDOWN_DUMMY     = 99519,
};

enum Events
{
    EVENT_BERSERK           = 1,
    EVENT_SHARDS_OF_TORMENT = 2,
    EVENT_BLAZE_OF_GLORY    = 3,
    EVENT_WAVE_OF_TORMENT   = 4,
    EVENT_INFERNO_BLADE     = 5,
    EVENT_DECIMATION_BLADE  = 6,
    EVENT_FINAL_COUNTDOWN   = 7,
    EVENT_CHECK_PLAYERS     = 8,
};

enum Adds
{
    NPC_SHARD_OF_TORMENT    = 53495, // 99258
};

class boss_baleroc : public CreatureScript
{
    public:
        boss_baleroc() : CreatureScript("boss_baleroc") { }

        CreatureAI* GetAI(Creature* pCreature) const
        {
            return new boss_balerocAI(pCreature);
        }

        struct boss_balerocAI : public BossAI
        {
            boss_balerocAI(Creature* pCreature) : BossAI(pCreature, DATA_BALEROC)
            {
                me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_GRIP, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_STUN, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FEAR, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_ROOT, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_FREEZE, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_HORROR, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SAPPED, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, true);
                me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_DISORIENTED, true);
                me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_CONFUSE, true);
                me->setActive(true);
            }

            std::map<uint32, uint8> tormentedPlayers;

            void InitializeAI()
            {
                if (!instance || static_cast<InstanceMap*>(me->GetMap())->GetScriptId() != sObjectMgr->GetScriptId(FLScriptName))
                    me->IsAIEnabled = false;
                else if (!me->isDead())
                    Reset();
            }

            bool AllowAchieve()
            {
                if (tormentedPlayers.empty())
                    return false;

                for (std::map<uint32, uint8>::const_iterator itr = tormentedPlayers.begin(); itr != tormentedPlayers.end(); ++itr)
                    if (itr->second > 3)
                        return false;

                return true;
            }

            void AddTormentedPlayer(uint32 guid)
            {
                tormentedPlayers[guid]++;
            }

            void Reset()
            {
                _Reset();

                tormentedPlayers.clear();

                instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLAZE_OF_GLORY);
            }

            void EnterCombat(Unit* attacker)
            {
                if (!instance->CheckRequiredBosses(DATA_BALEROC, me->GetEntry(), attacker->ToPlayer()))
                {
                    EnterEvadeMode();
                    instance->DoNearTeleportPlayers(FLEntrancePos);
                    return;
                }
                
                Talk(SAY_AGGRO);
                instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLAZE_OF_GLORY);
                events.ScheduleEvent(EVENT_BERSERK, 6 * MINUTE * IN_MILLISECONDS);
                events.ScheduleEvent(EVENT_BLAZE_OF_GLORY, 9000);
                events.ScheduleEvent(urand(0, 1)? EVENT_INFERNO_BLADE: EVENT_DECIMATION_BLADE, 30000);
                events.ScheduleEvent(EVENT_SHARDS_OF_TORMENT, 34000);
                if (IsHeroic())
                    events.ScheduleEvent(EVENT_FINAL_COUNTDOWN, 25000);
                events.ScheduleEvent(EVENT_CHECK_PLAYERS, 5000);
                instance->SetBossState(DATA_BALEROC, IN_PROGRESS);
            }

            void JustDied(Unit* /*killer*/)
            {
                instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_BLAZE_OF_GLORY);
                _JustDied();
                Talk(SAY_DEATH);

                AddSmoulderingAura(me);
            }
            
            void KilledUnit(Unit* who)
            {
                Talk(SAY_KILL);
            }

            void UpdateAI(const uint32 diff)
            {
                if (!UpdateVictim() || !CheckInArea(diff, 5767))
                    return;

                events.Update(diff);

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CHECK_PLAYERS:
                        {
                            Map::PlayerList const &PlayerList = instance->instance->GetPlayers();
                            if (!PlayerList.isEmpty())
                            {
                                for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                                {
                                    if (Player* pPlayer = i->getSource())
                                        if (pPlayer->GetPositionZ() > 70.0f)
                                            pPlayer->NearTeleportTo(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
                                }
                            }
                            events.ScheduleEvent(EVENT_CHECK_PLAYERS, 5000);
                            break;
                        }
                        case EVENT_BERSERK:
                            Talk(SAY_BERSERK);
                            DoCast(me, SPELL_BERSERK);
                            break;
                        case EVENT_BLAZE_OF_GLORY:
                            DoCast(me, SPELL_INCENDIARY_SOUL, true);
                            DoCastVictim(SPELL_BLAZE_OF_GLORY);
                            events.ScheduleEvent(EVENT_BLAZE_OF_GLORY, 9000);
                            break;
                        case EVENT_INFERNO_BLADE:
                            Talk(SAY_INFERNO);
                            DoCast(me, SPELL_INFERNO_BLADE);
                            events.ScheduleEvent(EVENT_DECIMATION_BLADE, 30000);
                            events.RescheduleEvent(EVENT_BLAZE_OF_GLORY, urand(7000, 9000));
                            break;
                        case EVENT_DECIMATION_BLADE:
                            Talk(SAY_DECIMATION);
                            DoCast(me, SPELL_DECIMATION_BLADE);
                            events.ScheduleEvent(EVENT_INFERNO_BLADE, 30000);
                            events.RescheduleEvent(EVENT_BLAZE_OF_GLORY, urand(7000, 9000));
                            break;
                        case EVENT_SHARDS_OF_TORMENT:
                            Talk(SAY_SHARDS);
                            me->CastCustomSpell(SPELL_SHARDS_OF_TORMENT_AOE, SPELLVALUE_MAX_TARGETS, RAID_MODE(1, 2, 1, 2), me);
                            events.ScheduleEvent(EVENT_SHARDS_OF_TORMENT, 34000);
                            break;
                        case EVENT_FINAL_COUNTDOWN:
                            DoCastAOE(SPELL_FINAL_COUNTDOWN);
                            events.ScheduleEvent(EVENT_FINAL_COUNTDOWN, 45000);
                            break;
                    }
                }
                
                if (me->HasAura(SPELL_INFERNO_BLADE))
                {
                    if (!me->HasUnitState(UNIT_STATE_CASTING) && me->isAttackReady() && me->IsWithinMeleeRange(me->getVictim()))
                    {
                        DoCastVictim(SPELL_INFERNO_BLADE_DMG, true);
                        me->resetAttackTimer();
                    }
                }
                else if (me->HasAura(SPELL_DECIMATION_BLADE) || me->HasAura(SPELL_DECIMATION_BLADE_25))
                {
                    if (!me->HasUnitState(UNIT_STATE_CASTING) && me->isAttackReady() && me->IsWithinMeleeRange(me->getVictim()))
                    {
                        DoCastVictim(SPELL_DECIMATION_BLADE_DMG, true);
                        me->resetAttackTimer();
                    }
                }
                else
                    DoMeleeAttackIfReady();
            }
        };
};

class npc_baleroc_shard_of_torment : public CreatureScript
{
    public:
        npc_baleroc_shard_of_torment() : CreatureScript("npc_baleroc_shard_of_torment") { }

        CreatureAI* GetAI(Creature* pCreature) const
        {
            return new npc_baleroc_shard_of_tormentAI(pCreature);
        }

        struct npc_baleroc_shard_of_tormentAI : public Scripted_NoMovementAI
        {
            npc_baleroc_shard_of_tormentAI(Creature* pCreature) : Scripted_NoMovementAI(pCreature)
            {
                me->SetReactState(REACT_PASSIVE);
                me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
            }

            bool bReady;
            uint32 uiReadyTimer;
            uint32 uiSelectTargetTimer;
            Player* curVictim;

            void Reset()
            {
                bReady = false;
                uiReadyTimer = 3000;
                uiSelectTargetTimer = 1000;
                curVictim = NULL;
                DoCast(me, SPELL_TORMENT_VISUAL, true);
            }

            void UpdateAI(const uint32 diff)
            {
                if (!bReady)
                {
                    if (uiReadyTimer <= diff)
                        bReady = true;
                    else
                        uiReadyTimer -= diff;

                    return;
                }
                
                if (uiSelectTargetTimer <= diff)
                {
                    if(Player* newVictim = me->SelectNearestPlayer(15.0f))
                    {
                        if (newVictim == curVictim)
                            return;
                
                        me->InterruptNonMeleeSpells(false);
                        curVictim = newVictim;
                        
                        if (Creature* pBaleroc = me->FindNearestCreature(NPC_BALEROC, 200.0f))
                            if (boss_baleroc::boss_balerocAI* BalerocAI = CAST_AI(boss_baleroc::boss_balerocAI, pBaleroc->GetAI()))
                                BalerocAI->AddTormentedPlayer(curVictim->GetGUIDLow());
                        
                        DoCast(curVictim, SPELL_TORMENT);
                    }
                    else
                    {
                        curVictim = NULL;
                        me->InterruptNonMeleeSpells(false);
                        DoCastAOE(SPELL_WAVE_OF_TORMENT);
                    }
                    uiSelectTargetTimer = 1000;
                }
                else
                    uiSelectTargetTimer -= diff;
            }
        };
};

class spell_baleroc_shards_of_torment_aoe : public SpellScriptLoader
{
    public:
        spell_baleroc_shards_of_torment_aoe() : SpellScriptLoader("spell_baleroc_shards_of_torment_aoe") { }

        class spell_baleroc_shards_of_torment_aoe_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_baleroc_shards_of_torment_aoe_SpellScript);

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                if (!GetCaster())
                    return;

                if (targets.empty())
                    return;

                switch (GetCaster()->GetMap()->GetDifficulty())
                {
                    case RAID_DIFFICULTY_10MAN_NORMAL:
                    case RAID_DIFFICULTY_10MAN_HEROIC:
                    {    
                        std::list<WorldObject*> meleetargets;

                        for (std::list<WorldObject*>::const_iterator itr = targets.begin(); itr != targets.end(); ++itr)
                        {
                            if (WorldObject* target = (*itr))
                            {
                                if (GetCaster()->GetDistance(target) < 20.0f)
                                    meleetargets.push_back(target);
                            }
                        }

                        if (!meleetargets.empty())
                        {
                            if (GetMembersWithoutAura(meleetargets, SPELL_BLAZE_OF_GLORY) >= 1)
                                meleetargets.remove_if(AuraCheck(SPELL_BLAZE_OF_GLORY));
                            
                            if (meleetargets.size() > 1)
                                Trinity::Containers::RandomResizeList(meleetargets, 1);

                            targets.clear();
                            targets.push_back(meleetargets.front());
                        }
                        else
                            Trinity::Containers::RandomResizeList(targets, 1);

                        break;
                    }
                    case RAID_DIFFICULTY_25MAN_NORMAL:
                    case RAID_DIFFICULTY_25MAN_HEROIC:
                    {    
                        std::list<WorldObject*> meleetargets;
                        std::list<WorldObject*> rangetargets;

                        for (std::list<WorldObject*>::const_iterator itr = targets.begin(); itr != targets.end(); ++itr)
                        {
                            if (WorldObject* target = (*itr))
                            {
                                if (GetCaster()->GetDistance(target) > 20.0f)
                                    rangetargets.push_back(target);
                                else
                                    meleetargets.push_back(target);
                            }
                        }

                        if (!meleetargets.empty() && rangetargets.empty() || meleetargets.empty() && !rangetargets.empty())
                        {
                            if (GetMembersWithoutAura(targets, SPELL_BLAZE_OF_GLORY) >= 2)
                                targets.remove_if(AuraCheck(SPELL_BLAZE_OF_GLORY));
                            
                            if (targets.size() > 2)
                                Trinity::Containers::RandomResizeList(targets, 2);
                        }
                        else if (!meleetargets.empty() && !rangetargets.empty())
                        {
                            if (GetMembersWithoutAura(meleetargets, SPELL_BLAZE_OF_GLORY) >= 1)
                                meleetargets.remove_if(AuraCheck(SPELL_BLAZE_OF_GLORY));
                            
                            if (meleetargets.size() > 1)
                                Trinity::Containers::RandomResizeList(meleetargets, 1);

                            if (GetMembersWithoutAura(rangetargets, SPELL_BLAZE_OF_GLORY) >= 1)
                                rangetargets.remove_if(AuraCheck(SPELL_BLAZE_OF_GLORY));
                            
                            if (rangetargets.size() > 1)
                                Trinity::Containers::RandomResizeList(rangetargets, 1);

                            targets.clear();;
                            targets.push_back(meleetargets.front());
                            targets.push_back(rangetargets.front());
                        }
                        break;
                    }
                }
            }

            void HandleDummy(SpellEffIndex /*effIndex*/)
            {
                if(!GetCaster() || !GetHitUnit())
                    return;

                GetCaster()->CastSpell(GetHitUnit(), SPELL_SHARDS_OF_TORMENT, true);
            }

            void Register()
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_baleroc_shards_of_torment_aoe_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_baleroc_shards_of_torment_aoe_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
            }

            private:

                uint32 GetMembersWithoutAura(std::list<WorldObject*> templist, uint32 spellId)
                {
                    uint32 count = 0;

                    if (templist.empty())
                        return 0;

                    for (std::list<WorldObject*>::const_iterator itr = templist.begin(); itr != templist.end(); ++itr)
                        if (WorldObject* unit = (*itr))
                            if (unit->ToUnit() && !unit->ToUnit()->HasAura(spellId))
                                count++;

                    return count;
                }

                class AuraCheck
                {
                    public:
                        AuraCheck(uint32 spellId) : _spellId(spellId) {}

                        bool operator()(WorldObject* unit)
                        {
                            return (!unit->ToUnit() || unit->ToUnit()->HasAura(_spellId));
                        }

                    private:
                        uint32 _spellId;
                };

                class DistanceCheckWithoutTanks
                {
                    public:
                        DistanceCheckWithoutTanks(WorldObject* searcher, float range) : _searcher(searcher), _range(range) {}

                        bool operator()(WorldObject* unit)
                        {
                            return (!unit->ToUnit() || (_searcher->GetDistance(unit) > _range && unit->ToUnit()->HasAura(SPELL_BLAZE_OF_GLORY)));
                        }

                    private:
                        WorldObject* _searcher;
                        float _range;
                };
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_baleroc_shards_of_torment_aoe_SpellScript();
        }
};

class spell_baleroc_tormented_aoe : public SpellScriptLoader
{
    public:
        spell_baleroc_tormented_aoe() : SpellScriptLoader("spell_baleroc_tormented_aoe") { }

        class spell_baleroc_tormented_aoe_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_baleroc_tormented_aoe_SpellScript);

            void HandleDummy(SpellEffIndex /*effIndex*/)
            {
                if (!GetCaster() || !GetHitUnit())
                    return;

                if ((GetCaster() == GetHitUnit()) ||  GetHitUnit()->HasAura(SPELL_TORMENTED_25H))
                    return;

                GetHitUnit()->CastSpell(GetHitUnit(), SPELL_TORMENTED_25H, true);
            }

            void Register()
            {
                OnEffectHitTarget += SpellEffectFn(spell_baleroc_tormented_aoe_SpellScript::HandleDummy, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_baleroc_tormented_aoe_SpellScript();
        }
};

class spell_baleroc_final_countdown : public SpellScriptLoader
{
    public:
        spell_baleroc_final_countdown() : SpellScriptLoader("spell_baleroc_final_countdown") { }

        class spell_baleroc_final_countdown_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_baleroc_final_countdown_SpellScript);
            
            bool Load()
            {
                bCanLink = false;
                target1 = NULL;
                target2 = NULL;
                return true;
            }

            void HandleDummy()
            {
                if (!GetCaster())
                    return;

                if (bCanLink &&target1 && target1->isAlive() && target1->IsInWorld() &&
                    target2 && target2->isAlive() && target2->IsInWorld())
                {
                    GetCaster()->CastSpell(target1, SPELL_FINAL_COUNTDOWN_AURA, true);
                    GetCaster()->CastSpell(target2, SPELL_FINAL_COUNTDOWN_AURA, true);
                    target1->CastSpell(target2, SPELL_FINAL_COUNTDOWN_DUMMY, true);
                }
            }
            
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                //if (!targets.empty() && GetCaster() && GetCaster()->getVictim())
                //    targets.remove_if(GuidCheck(GetCaster()->getVictim()->GetGUID()));

                if (targets.size() < 2)
                    bCanLink = false;
                else
                {
                    std::list<WorldObject*>::const_iterator itr = targets.begin();
                    target1 = (*itr)->ToUnit();
                    ++itr;
                    target2 = (*itr)->ToUnit();
                    bCanLink = true;
                }
            }

            void Register()
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_baleroc_final_countdown_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                AfterCast += SpellCastFn(spell_baleroc_final_countdown_SpellScript::HandleDummy);
            }

        private:
            bool bCanLink;
            Unit* target1;
            Unit* target2;

            class GuidCheck
            {
                public:
                    GuidCheck(uint64 guid) : _guid(guid) {}

                    bool operator()(WorldObject* unit)
                    {
                        return (unit->GetGUID() == _guid);
                    }

                private:
                    uint64 _guid;
            };
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_baleroc_final_countdown_SpellScript();
        }
};

class spell_baleroc_final_countdown_script : public SpellScriptLoader
{
    public:
        spell_baleroc_final_countdown_script() : SpellScriptLoader("spell_baleroc_final_countdown_script") { }

        class spell_baleroc_final_countdown_script_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_baleroc_final_countdown_script_SpellScript);

            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (!GetCaster() || !GetHitUnit() || (GetCaster() == GetHitUnit()))
                    return;

                GetCaster()->RemoveAurasDueToSpell(SPELL_FINAL_COUNTDOWN_AURA);
                GetHitUnit()->RemoveAurasDueToSpell(SPELL_FINAL_COUNTDOWN_AURA);
            }

            void Register()
            {
                OnEffectHitTarget += SpellEffectFn(spell_baleroc_final_countdown_script_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
            }
        };

        SpellScript* GetSpellScript() const
        {
            return new spell_baleroc_final_countdown_script_SpellScript();
        }
};

class achievement_share_the_pain : public AchievementCriteriaScript
{
    public:
        achievement_share_the_pain() : AchievementCriteriaScript("achievement_share_the_pain") { }

        bool OnCheck(Player* source, Unit* target)
        {
            if (!target)
                return false;

            if (boss_baleroc::boss_balerocAI* BalerocAI = CAST_AI(boss_baleroc::boss_balerocAI, target->GetAI()))
                return BalerocAI->AllowAchieve();

            return false;
        }
};

void AddSC_boss_baleroc()
{
    new boss_baleroc();
    new npc_baleroc_shard_of_torment();
    new spell_baleroc_shards_of_torment_aoe();
    new spell_baleroc_tormented_aoe();
    new spell_baleroc_final_countdown();
    new spell_baleroc_final_countdown_script();
    new achievement_share_the_pain();
}