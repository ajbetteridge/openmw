#include "autocalcspell.hpp"

#include <climits>

#include "../mwworld/esmstore.hpp"

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"


namespace MWMechanics
{

    struct SchoolCaps
    {
        int mCount;
        int mLimit;
        bool mReachedLimit;
        int mMinCost;
        std::string mWeakestSpell;
    };

    std::set<std::string> autoCalcNpcSpells(const int *actorSkills, const int *actorAttributes, const ESM::Race* race)
    {
        const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        static const float fNPCbaseMagickaMult = gmst.find("fNPCbaseMagickaMult")->getFloat();
        float baseMagicka = fNPCbaseMagickaMult * actorAttributes[ESM::Attribute::Intelligence];

        static const std::string schools[] = {
            "alteration", "conjuration", "destruction", "illusion", "mysticism", "restoration"
        };
        static int iAutoSpellSchoolMax[6];
        static bool init = false;
        if (!init)
        {
            for (int i=0; i<6; ++i)
            {
                const std::string& gmstName = "iAutoSpell" + schools[i] + "Max";
                iAutoSpellSchoolMax[i] = gmst.find(gmstName)->getInt();
            }
            init = true;
        }

        std::map<int, SchoolCaps> schoolCaps;
        for (int i=0; i<6; ++i)
        {
            SchoolCaps caps;
            caps.mCount = 0;
            caps.mLimit = iAutoSpellSchoolMax[i];
            caps.mReachedLimit = iAutoSpellSchoolMax[i] <= 0;
            caps.mMinCost = INT_MAX;
            caps.mWeakestSpell.clear();
            schoolCaps[i] = caps;
        }

        std::set<std::string> selectedSpells;

        const MWWorld::Store<ESM::Spell> &spells =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>();
        for (MWWorld::Store<ESM::Spell>::iterator iter = spells.begin(); iter != spells.end(); ++iter)
        {
            const ESM::Spell* spell = &*iter;

            if (spell->mData.mType != ESM::Spell::ST_Spell)
                continue;
            if (!(spell->mData.mFlags & ESM::Spell::F_Autocalc))
                continue;
            static const int iAutoSpellTimesCanCast = gmst.find("iAutoSpellTimesCanCast")->getInt();
            if (baseMagicka < iAutoSpellTimesCanCast * spell->mData.mCost)
                continue;

            if (race && std::find(race->mPowers.mList.begin(), race->mPowers.mList.end(), spell->mId) != race->mPowers.mList.end())
                continue;

            if (!attrSkillCheck(spell, actorSkills, actorAttributes))
                continue;

            int school;
            float skillTerm;
            calcWeakestSchool(spell, actorSkills, school, skillTerm);
            assert(school >= 0 && school < 6);
            SchoolCaps& cap = schoolCaps[school];

            if (cap.mReachedLimit && spell->mData.mCost <= cap.mMinCost)
                continue;

            static const float fAutoSpellChance = gmst.find("fAutoSpellChance")->getFloat();
            if (calcAutoCastChance(spell, actorSkills, actorAttributes, school) < fAutoSpellChance)
                continue;

            selectedSpells.insert(spell->mId);

            if (cap.mReachedLimit)
            {
                selectedSpells.erase(cap.mWeakestSpell);

                // Note: not school specific
                cap.mMinCost = INT_MAX;
                for (std::set<std::string>::iterator weakIt = selectedSpells.begin(); weakIt != selectedSpells.end(); ++weakIt)
                {
                    const ESM::Spell* testSpell = spells.find(*weakIt);
                    if (testSpell->mData.mCost < cap.mMinCost) // XXX what if 2 candidates have the same cost?
                    {
                        cap.mMinCost = testSpell->mData.mCost;
                        cap.mWeakestSpell = testSpell->mId;
                    }
                }
            }
            else
            {
                cap.mCount += 1;
                if (cap.mCount == cap.mLimit)
                    cap.mReachedLimit = true;

                if (spell->mData.mCost < cap.mMinCost)
                {
                    cap.mWeakestSpell = spell->mId;
                    cap.mMinCost = spell->mData.mCost;
                }
            }
        }
        return selectedSpells;
    }

    bool attrSkillCheck (const ESM::Spell* spell, const int* actorSkills, const int* actorAttributes)
    {
        const std::vector<ESM::ENAMstruct>& effects = spell->mEffects.mList;
        for (std::vector<ESM::ENAMstruct>::const_iterator effectIt = effects.begin(); effectIt != effects.end(); ++effectIt)
        {
            const ESM::MagicEffect* magicEffect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(effectIt->mEffectID);
            static const int iAutoSpellAttSkillMin = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("iAutoSpellAttSkillMin")->getInt();

            if ((magicEffect->mData.mFlags & ESM::MagicEffect::TargetSkill))
            {
                assert (effectIt->mSkill >= 0 && effectIt->mSkill < ESM::Skill::Length);
                if (actorSkills[effectIt->mSkill] < iAutoSpellAttSkillMin)
                    return false;
            }

            if ((magicEffect->mData.mFlags & ESM::MagicEffect::TargetAttribute))
            {
                assert (effectIt->mAttribute >= 0 && effectIt->mAttribute < ESM::Attribute::Length);
                if (actorAttributes[effectIt->mAttribute] < iAutoSpellAttSkillMin)
                    return false;
            }
        }

        return true;
    }

    ESM::Skill::SkillEnum mapSchoolToSkill(int school)
    {
        std::map<int, ESM::Skill::SkillEnum> schoolSkillMap; // maps spell school to skill id
        schoolSkillMap[0] = ESM::Skill::Alteration;
        schoolSkillMap[1] = ESM::Skill::Conjuration;
        schoolSkillMap[3] = ESM::Skill::Illusion;
        schoolSkillMap[2] = ESM::Skill::Destruction;
        schoolSkillMap[4] = ESM::Skill::Mysticism;
        schoolSkillMap[5] = ESM::Skill::Restoration;
        assert(schoolSkillMap.find(school) != schoolSkillMap.end());
        return schoolSkillMap[school];
    }

    void calcWeakestSchool (const ESM::Spell* spell, const int* actorSkills, int& effectiveSchool, float& skillTerm)
    {
        float minChance = FLT_MAX;

        const ESM::EffectList& effects = spell->mEffects;
        for (std::vector<ESM::ENAMstruct>::const_iterator it = effects.mList.begin(); it != effects.mList.end(); ++it)
        {
            const ESM::ENAMstruct& effect = *it;
            float x = effect.mDuration;

            const ESM::MagicEffect* magicEffect = MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(effect.mEffectID);
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::UncappedDamage))
                x = std::max(1.f, x);

            x *= 0.1f * magicEffect->mData.mBaseCost;
            x *= 0.5f * (effect.mMagnMin + effect.mMagnMax);
            x += effect.mArea * 0.05f * magicEffect->mData.mBaseCost;
            if (effect.mRange == ESM::RT_Target)
                x *= 1.5f;

            static const float fEffectCostMult = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fEffectCostMult")->getFloat();
            x *= fEffectCostMult;

            float s = 2.f * actorSkills[mapSchoolToSkill(magicEffect->mData.mSchool)];
            if (s - x < minChance)
            {
                minChance = s - x;
                effectiveSchool = magicEffect->mData.mSchool;
                skillTerm = s;
            }
        }
    }

    float calcAutoCastChance(const ESM::Spell *spell, const int *actorSkills, const int *actorAttributes, int effectiveSchool)
    {
        if (spell->mData.mType != ESM::Spell::ST_Spell)
            return 100.f;

        if (spell->mData.mFlags & ESM::Spell::F_Always)
            return 100.f;

        float skillTerm;
        if (effectiveSchool != -1)
            skillTerm = 2.f * actorSkills[mapSchoolToSkill(effectiveSchool)];
        else
            calcWeakestSchool(spell, actorSkills, effectiveSchool, skillTerm); // Note effectiveSchool is unused after this

        float castChance = skillTerm - spell->mData.mCost + 0.2f * actorAttributes[ESM::Attribute::Willpower] + 0.1f * actorAttributes[ESM::Attribute::Luck];
        return castChance;
    }
}
