//无人岛剧本规则的数值自检：公式与 umasim 的 mujinto_memo.md 对照，并跑一局打印剧本状态
#include <iostream>
#include <random>
#include <string>
#include "../Game/Game.h"
#include "../NeuralNet/Evaluator.h"
#include "../Search/Search.h"
#include "../GameDatabase/GameDatabase.h"
#include "tests.h"
using namespace std;

namespace
{
  int failCount = 0;
  int checkCount = 0;
  void check(bool ok, const string& what)
  {
    checkCount++;
    if (!ok)
    {
      failCount++;
      cout << "\033[31m[FAIL]\033[0m " << what << endl;
    }
  }
  void checkEq(int got, int expected, const string& what)
  {
    check(got == expected, what + "：得到 " + to_string(got) + "，应为 " + to_string(expected));
  }

  //速卡x5 + 塔克布莱恩，cardId末位是突破数
  Game newTestGame(mt19937_64& rand, int tuckerCardId)
  {
    int cards[6] = { tuckerCardId,301724,301614,301784,301874,301734 };
    int blue[5] = { 18,0,0,0,0 };
    int bonus[6] = { 10,10,30,0,10,70 };
    Game game;
    game.newGame(rand, GameSettings(), 102401, 5, cards, blue, bonus);
    return game;
  }

  void clearDistribution(Game& game)
  {
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 5; j++)
        game.personDistribution[i][j] = -1;
  }

  void testFacility()
  {
    //memo：本能 1→1→2→2→3 合计9，熟练 1→1→1→1→2 合计6，海之家 2→3→3 合计8
    int instinct = 0, skilled = 0, house = 0;
    for (int lv = 1; lv <= 5; lv++)
    {
      instinct += MujintoFacility(MJ_speed, lv, false).space();
      skilled += MujintoFacility(MJ_speed, lv, lv >= 3).space();
    }
    for (int lv = 1; lv <= 3; lv++)
      house += MujintoFacility(MJ_house, lv, false).space();
    checkEq(instinct, 9, "本能设施总格数");
    checkEq(skilled, 6, "熟练设施总格数");
    checkEq(house, 8, "海之家总格数");

    //memo：塔克Lv41以上合计格数 4→9→15→21→29
    int total = 0;
    int expected[5] = { 4,9,15,21,29 };
    for (int p = 0; p < 5; p++)
    {
      total += GameConstants::MJ_FacilitySpace[2][p];
      checkEq(total, expected[p], "Lv41以上第" + to_string(p) + "期累计格数");
    }
  }

  void testLinkMode(mt19937_64& rand)
  {
    checkEq(newTestGame(rand, 302574).mj_linkMode, 2, "SSR塔克满破 linkMode");
    checkEq(newTestGame(rand, 302573).mj_linkMode, 2, "SSR塔克3破(Lv45) linkMode");
    checkEq(newTestGame(rand, 302572).mj_linkMode, 1, "SSR塔克2破(Lv40) linkMode");
    checkEq(newTestGame(rand, 101284).mj_linkMode, 1, "R塔克满破(Lv40) linkMode");
    Game g = newTestGame(rand, 302574);
    check(abs(g.friend_vitalBonus - 1.6) < 1e-6 && abs(g.friend_statusBonus - 1.3) < 1e-6, "SSR塔克满破 事件回复60%/效果30%");
  }

  void testPioneerPt(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    g.turn = 20;
    g.mj_bonusPioneerPt = 10;
    //memo：TRUNC((60+人数×6)×(100+评价会加成+友情20)/100)
    checkEq(g.mj_calcTrainingPioneerPt(5, true), (60 + 30) * 130 / 100, "训练发展pt（5人，友情）");
    checkEq(g.mj_calcTrainingPioneerPt(3, false), (60 + 18) * 110 / 100, "训练发展pt（3人）");
    checkEq(g.mj_calcRacePioneerPt(true), 20 * 310 / 100, "目标比赛发展pt");
    checkEq(g.mj_calcRacePioneerPt(false), 20 * 110 / 100, "目标外比赛发展pt（海之家Lv2以下）");
    g.mj_facilityLevel[MJ_house] = 3;
    checkEq(g.mj_calcRacePioneerPt(false), 20 * 310 / 100, "目标外比赛发展pt（海之家Lv3）");
    g.turn = 1;
    checkEq(g.mj_calcTrainingPioneerPt(5, true), 0, "第2回合之前没有发展pt");
    g.turn = 60;
    checkEq(g.mj_calcTrainingPioneerPt(5, true), 0, "第61回合之后没有发展pt");
  }

  void testBuildAndEvaluation(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    //第2回合后：第0期计划，4格
    g.turn = 1;
    g.mj_evaluationAndPlan(rand, 0);
    checkEq(g.mj_requiredPt1, 200, "第0期 requiredPt1");
    checkEq(g.mj_requiredPt2, 400, "第0期 requiredPt2");
    checkEq(g.mj_guestNum, 5, "第0期后嘉宾人数（10人-自己5张卡）");
    //建设计划由ST_plan逐个选择
    check(g.mj_planPending, "评价会后进入制定计划");
    g.stage = ST_plan;
    checkEq(int(g.getAllLegalActions().size()), 6, "Lv1时6种设施都只能选本能");
    check(!g.isLegal(Action(ST_plan, MJ_speed * 2 + 1)), "Lv1不能选熟练");
    while (g.stage == ST_plan)
      g.applyAction(rand, Evaluator::handWrittenStrategy(g));
    checkEq(g.mj_planSpaceUsed(), 4, "计划占满4格");
    check(g.mj_plan[0].type == MJ_house, "手写策略先建海之家");
    check(!g.mj_planPending && g.stage == ST_distribute, "计划选完进入下回合");

    g.turn = 5;
    g.mj_addPioneerPt(199);
    checkEq(g.mj_facilityLevel[MJ_house], 0, "未到requiredPt1不建设");
    g.mj_addPioneerPt(1);
    checkEq(g.mj_facilityLevel[MJ_house], 1, "到requiredPt1建前半（海之家2格）");
    checkEq(g.mj_ticket, 1, "到requiredPt1获得岛训练券");
    g.mj_addPioneerPt(300);
    checkEq(g.mj_planNum, 0, "到requiredPt2全部建成");
    checkEq(g.mj_ticket, 1, "岛训练券最多1张");

    //第1次评价会，大好评，超出100pt：体力+10
    g.turn = 11;
    g.vital = 50;
    int status0 = g.fiveStatus[4];
    int pt0 = g.skillPt;
    g.mj_evaluationAndPlan(rand, 1);
    checkEq(g.mj_evalResult[1], 2, "第1次评价会大好评");
    checkEq(g.vital, 60, "大好评超出100pt体力+10");
    check(g.fiveStatus[4] - status0 >= 10, "大好评全属性+10（智力）");
    check(g.skillPt - pt0 >= 60, "大好评pt+60");
    checkEq(g.mj_bonusTrainingEffect, 10, "第1次大好评后训练效果+10%");
    checkEq(g.mj_bonusPioneerPt, 5, "第1次大好评后发展pt+5%");
    checkEq(g.mj_requiredPt2, 500, "第1期 requiredPt2");

    //第2次评价会，好评
    g.turn = 23;
    g.mj_pioneerPt = 499;
    int lv0 = g.trainLevelCount[0];
    g.mj_evaluationAndPlan(rand, 2);
    checkEq(g.mj_evalResult[2], 1, "第2次评价会好评");
    checkEq(g.mj_bonusTrainingEffect, 10, "第2次好评后训练效果+10%");
    checkEq(g.trainLevelCount[0] - lv0, 4, "第2次评价会后训练等级+1");
    checkEq(g.mj_guestNum, 8, "第2次评价会后嘉宾人数");

    //合宿期间不建设，合宿结束后补建
    g.turn = 35;
    g.mj_evaluationAndPlan(rand, 3);
    g.turn = 37;
    int planNum0 = g.mj_planNum;
    g.mj_addPioneerPt(g.mj_requiredPt2);
    checkEq(g.mj_planNum, planNum0, "合宿期间不建设");
    g.turn = 39;
    g.mj_upgradeAfterCamp();
    checkEq(g.mj_planNum, 0, "合宿结束后补建");
  }

  void testPlanChoice(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    g.turn = 23;
    g.mj_facilityLevel[MJ_speed] = 2;
    g.mj_facilityLevel[MJ_stamina] = 3;
    g.mj_facilityJukuren[MJ_stamina] = true;
    g.mj_facilityLevel[MJ_house] = 3;
    g.mj_evaluationAndPlan(rand, 2);
    g.stage = ST_plan;
    check(g.isLegal(Action(ST_plan, MJ_speed * 2)) && g.isLegal(Action(ST_plan, MJ_speed * 2 + 1)), "Lv3可以选本能或熟练");
    check(!g.isLegal(Action(ST_plan, MJ_stamina * 2)) && g.isLegal(Action(ST_plan, MJ_stamina * 2 + 1)), "Lv4沿用熟练");
    check(!g.isLegal(Action(ST_plan, MJ_house * 2)), "海之家最多Lv3");
    g.applyAction(rand, Action(ST_plan, MJ_speed * 2 + 1));
    check(!g.isLegal(Action(ST_plan, MJ_speed * 2)), "同一期计划里同种设施只能一次");
    checkEq(g.mj_planSpaceUsed(), 1, "速Lv3熟练1格");
    //第2期 linkMode2 是6格
    while (g.stage == ST_plan)
      g.applyAction(rand, g.getAllLegalActions()[0]);
    check(g.mj_planSpaceUsed() <= 6, "计划不超过格数");
    check(!g.mj_hasPlanCandidate() || g.mj_planSpaceUsed() == 6, "选到放不下为止");
    //第5次评价会后没有计划
    g.turn = 59;
    g.mj_evaluationAndPlan(rand, 5);
    check(!g.mj_planPending, "第5次评价会后不制定计划");
  }

  void testTrainingValue(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    g.turn = 20;
    g.motivation = 3;
    g.vital = 50;
    for (int i = 0; i < 5; i++)
      g.trainLevelCount[i] = 0;
    for (int i = 0; i < 5; i++)
      g.fiveStatusBonus[i] = 0;
    for (int i = 0; i < 6; i++)
      g.persons[i].friendship = 0;
    g.mj_bonusTrainingEffect = 0;

    //空训练：速训练Lv1 = 12/0/1/0/0/6，体力-20
    clearDistribution(g);
    g.calculateTrainingValue();
    checkEq(g.trainValue[0][0], 12, "空速训练 速");
    checkEq(g.trainValue[0][2], 1, "空速训练 力");
    checkEq(g.trainValue[0][5], 6, "空速训练 pt");
    checkEq(g.trainVitalChange[0], -20, "空速训练 体力");
    checkEq(g.trainValue[4][4], 8, "空智训练 智");
    checkEq(g.trainVitalChange[4], 5, "空智训练 体力");

    //3个嘉宾：人头1.15
    g.personDistribution[0][0] = PS_guest0;
    g.personDistribution[0][1] = PS_guest0 + 1;
    g.personDistribution[0][2] = PS_guest0 + 2;
    g.calculateTrainingValue();
    checkEq(g.trainValue[0][0], int(12 * 1.15 + 1e-4), "速训练+3嘉宾 速");
    checkEq(g.mj_trainPioneerPt[0], 60 + 3 * 6, "速训练+3嘉宾 发展pt");

    //上层=下层×评价会训练效果
    g.mj_bonusTrainingEffect = 45;
    g.calculateTrainingValue();
    int lower = int(12 * 1.15 + 1e-4);
    checkEq(g.trainValueLower[0][0], lower, "速训练 下层");
    checkEq(g.trainValue[0][0], lower + lower * 45 / 100, "速训练 上层=下层×45%");

    //岛合宿：未建设施 速 14/0/1/0/0/8，体力-15；建了速Lv5本能后 0变1，加速+7力+1pt+2，训练效果+50%
    g.turn = 37;
    g.mj_bonusTrainingEffect = 0;
    clearDistribution(g);
    g.calculateTrainingValue();
    checkEq(g.trainValue[0][0], 14, "岛合宿 空速训练 速");
    checkEq(g.trainValue[0][1], 0, "岛合宿 空速训练 耐（未建设施）");
    checkEq(g.trainVitalChange[0], -15, "岛合宿 体力-15");
    g.mj_facilityLevel[MJ_speed] = 5;
    g.mj_facilityJukuren[MJ_speed] = false;
    g.calculateTrainingValue();
    checkEq(g.trainValueLower[0][0], 14 + 7, "岛合宿 速Lv5本能 速下层");
    checkEq(g.trainValueLower[0][1], 1, "岛合宿 速Lv5本能 耐下层");
    checkEq(g.trainValueLower[0][2], 1 + 1, "岛合宿 速Lv5本能 力下层");
    checkEq(g.trainValueLower[0][5], 8 + 2, "岛合宿 速Lv5本能 pt下层");
    checkEq(g.trainValue[0][0], 21 + 21 * 50 / 100, "岛合宿 速Lv5本能 上层+50%");
    checkEq(g.mj_specialtyRateUp(0), 80 + 100, "岛合宿 速Lv5本能 得意率");
    g.turn = 20;
    checkEq(g.mj_specialtyRateUp(0), 80, "平时 速Lv5本能 得意率");
  }

  //把一张卡的训练加成全部清零，用来对照 memo 里“やる気トレ効果0”的例子
  void zeroCardEffect(Game& g, int p)
  {
    SupportCard& c = g.persons[p].cardParam;
    c.youQingBasic = c.ganJingBasic = c.xunLianBasic = 0;
    for (int i = 0; i < 6; i++)
      c.bonusBasic[i] = 0;
    c.uniqueEffectType = 0;
    c.uniqueEffectParam.clear();
  }

  void testIslandTraining(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    g.turn = 20;
    g.stage = ST_train;
    g.isRacing = false;
    g.isAiJiao = false;
    g.motivation = 5;
    g.mj_ticket = 1;
    g.mj_bonusTrainingEffect = 0;
    g.mj_bonusPioneerPt = 0;
    for (int i = 0; i < 6; i++)
      g.mj_facilityLevel[i] = 0;
    for (int i = 0; i < 5; i++)
      g.fiveStatusBonus[i] = 0;
    for (int i = 0; i < 6; i++)
      g.persons[i].friendship = 0;
    g.mj_islandHouseNum = 0;

    //memo：绝好调、没人参加，智+12（10×1.2）
    clearDistribution(g);
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[4], 12, "岛训练 无人 智");
    checkEq(g.mj_islandValueLower[0], 9, "岛训练 无人 速");
    checkEq(g.mj_islandValue[4], 12, "岛训练 无设施 上层为0");

    //memo：设施上的嘉宾每人+0.02，3人pt+8（7×1.2×1.06），4人pt+9（7×1.2×1.08）
    g.mj_facilityLevel[MJ_speed] = 1;
    for (int h = 0; h < 3; h++)
      g.personDistribution[0][h] = PS_guest0 + h;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[5], 8, "岛训练 3嘉宾 pt");
    g.personDistribution[0][3] = PS_guest0 + 3;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[5], 9, "岛训练 4嘉宾 pt");
    checkEq(g.mj_islandPioneerPt, 60 + 4 * 6, "岛训练 4人 发展pt（umasim ×6）");

    //按 umasim，站在未建设施上的人也照常参加（只是没有设施加成）
    g.mj_facilityLevel[MJ_speed] = 0;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[5], 9, "岛训练 未建设施上的嘉宾也参加");

    //memo：智设施Lv1、1张训练效果0的卡，智+13（11×1.2×1.05）；再加1个嘉宾+14（11×1.2×1.07）
    clearDistribution(g);
    g.mj_facilityLevel[MJ_wiz] = 1;
    zeroCardEffect(g, 1);
    g.personDistribution[4][0] = 1;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[4], 13, "岛训练 智Lv1+1卡 智");
    g.personDistribution[4][1] = PS_guest0;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[4], 14, "岛训练 智Lv1+1卡+1嘉宾 智");

    //海之家里的卡每人+0.01：智 11×1.2×1.08=14.256
    zeroCardEffect(g, 2);
    g.mj_facilityLevel[MJ_house] = 1;
    g.mj_islandHouse[0] = 2;
    g.mj_islandHouseNum = 1;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandValueLower[4], 14, "岛训练 +海之家1卡 智");
    g.mj_islandHouseNum = 0;

    //两个设施同时友情（要有海之家）：上层 = 下层×(25+海之家Lv1的5)%
    clearDistribution(g);
    g.mj_facilityLevel[MJ_speed] = 1;
    zeroCardEffect(g, 3);
    g.persons[1].friendship = 100;
    g.persons[1].cardParam.cardType = 4;
    g.persons[3].friendship = 85;
    g.persons[3].cardParam.cardType = 0;
    g.personDistribution[4][0] = 1;
    g.personDistribution[0][0] = 3;
    g.mj_calculateIslandTraining(nullptr);
    checkEq(g.mj_islandFriendPositions, 2, "岛训练 友情设施数");
    int lower = g.mj_islandValueLower[4];
    checkEq(g.mj_islandValue[4], lower + lower * (25 + 5) / 100, "岛训练 2设施友情 上层+25%+海之家5%");
    checkEq(g.mj_islandPioneerPt, (60 + 2 * 6) * 120 / 100, "岛训练 友情时发展pt+20%");

    //执行：券-1，参加的卡羁绊+10，没参加的不变（umasim）
    int bond0 = g.persons[2].friendship;
    int bond1 = g.persons[3].friendship;
    int status0 = g.fiveStatus[4];
    int value = g.mj_islandValue[4];
    g.mj_applyIslandTraining(rand);
    checkEq(g.mj_ticket, 0, "岛训练 消耗1张券");
    checkEq(g.persons[2].friendship - bond0, 0, "岛训练 没参加的卡羁绊不变");
    checkEq(g.persons[3].friendship - bond1, std::min(10, 100 - bond1), "岛训练 参加的卡羁绊+10");
    check(g.fiveStatus[4] - status0 >= value, "岛训练 属性增加");
    g.stage = ST_train;
    check(!g.mj_isIslandTrainingAvailable(), "没券时不能岛训练");
  }

  //蒙特卡洛搜索能处理建设计划阶段
  void testPlanSearch(mt19937_64& rand)
  {
    Game g = newTestGame(rand, 302574);
    g.continueUntilNextDecision(rand);
    while (!g.isEnd() && g.stage != ST_plan)
      g.applyActionUntilNextDecision(rand, Evaluator::handWrittenStrategy(g));
    check(g.stage == ST_plan, "对局中会进入制定计划阶段");
    Search search(nullptr, 16, 2, SearchParam(64, 0));
    Action a = search.runSearch(g, rand);
    check(a.stage == ST_plan && g.isLegal(a), "搜索在制定计划阶段给出合法选择：" + a.toString(g));
  }

  //跑一局，每回合打印剧本状态
  void runOneGame(mt19937_64& rand, int firstCard, const string& label)
  {
    cout << "---- 示例对局：" << label << " ----" << endl;
    Game g = newTestGame(rand, firstCard);
    g.continueUntilNextDecision(rand);
    const int checkpoints[8] = { 2,12,24,36,40,48,60,78 };//这些回合结束后打印
    int nextCheckpoint = 0;
    int islandCount = 0;
    while (!g.isEnd())
    {
      Action a = Evaluator::handWrittenStrategy(g);
      if (a.stage == ST_train && a.idx == T_island)
        { islandCount++; cout << "  岛训练@" << g.turn + 1 << ": "; for (int s = 0; s < 6; s++) cout << g.mj_islandValue[s] << " "; cout << "发展pt " << g.mj_islandPioneerPt << " 友情设施 " << g.mj_islandFriendPositions << endl; }
      g.applyActionUntilNextDecision(rand, a);
      while (nextCheckpoint < 8 && g.turn >= checkpoints[nextCheckpoint])
      {
        int turn = checkpoints[nextCheckpoint] - 1;
        nextCheckpoint++;
        cout << "第" << turn + 1 << "回合后：设施";
        for (int f = 0; f < 6; f++)
          cout << " " << int(g.mj_facilityLevel[f]);
        cout << "，发展pt " << g.mj_pioneerPt << "/" << g.mj_requiredPt2 << "，券 " << g.mj_ticket << "，嘉宾 " << g.mj_guestNum
          << "，属性 " << g.fiveStatus[0] << "/" << g.fiveStatus[1] << "/" << g.fiveStatus[2] << "/" << g.fiveStatus[3] << "/" << g.fiveStatus[4]
          << "，pt " << g.skillPt << endl;
      }
    }
    cout << "评价会结果：";
    for (int i = 1; i <= 5; i++)
      cout << (g.mj_evalResult[i] == 2 ? "大好评 " : "好评 ");
    cout << "，友人出行 " << g.friend_outgoingNum << "/5，岛训练 " << islandCount << " 次，最终分 " << g.finalScore() << endl;
  }
}

void main_testMujinto()
{
  GameDatabase::loadTranslation("../db/text_data.json");
  GameDatabase::loadUmas("../db/umaDB.json");
  GameDatabase::loadDBCards("../db/cardDB.json");

  mt19937_64 rand(12345);
  try
  {
    testFacility();
    testLinkMode(rand);
    testPioneerPt(rand);
    testBuildAndEvaluation(rand);
    testPlanChoice(rand);
    testTrainingValue(rand);
    testIslandTraining(rand);
    testPlanSearch(rand);
    cout << "自检：" << checkCount - failCount << "/" << checkCount << " 通过" << endl;
    runOneGame(rand, 302574, "SSR塔克满破");
    runOneGame(rand, 101284, "R塔克满破");
    runOneGame(rand, 302474, "不带塔克");
  }
  catch (const char* e)
  {
    cout << "异常：" << e << endl;
  }
  catch (string e)
  {
    cout << "异常：" << e << endl;
  }
}
