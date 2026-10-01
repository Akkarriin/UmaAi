#include <iostream>
#include <sstream>
#include <cassert>
#include "vector"
#include "../External/json.hpp"
#include "Protocol.h"
#include "Game.h"
using namespace std;
using json = nlohmann::json;

bool Game::loadGameFromJson(std::string jsonStr)
{
  if (jsonStr == "[test]" || jsonStr == "{\"Result\":1,\"Reason\":null}")
  {
    std::cout << "已成功与URA建立连接，但暂未接收到回合信息，等待游戏开始" << std::endl;
    return false;
  }
  try
  {
    json j = json::parse(jsonStr, nullptr, true, true);
    auto rand = mt19937_64(114514);
    int newcards[6];
    int newzmbluecount[5];
    for (int i = 0; i < 6; i++) {
        newcards[i] = j["cardId"][i];
        if(i<5){ newzmbluecount[i] = j["zhongMaBlueCount"][i]; }
        
    }
    //int zhongmaBlue[5] = { 18,0,0,0,0 };
    int zhongmaBonus[6] = { 5,5,30,30,5,200 };
    newGame(rand,GameSettings(), j["umaId"], j["umaStar"], newcards, newzmbluecount, zhongmaBonus);
    assert(friend_type == 0 || friend_personId == j["friend_personId"]);
    
    gameSettings.ptScoreRate = j.contains("ptScoreRate") ? double(j["ptScoreRate"]) : GameConstants::ScorePtRateDefault;
    
    turn = j["turn"];
    vital = j["vital"];
    maxVital = j["maxVital"];
    motivation = j["motivation"];
    for (int i = 0; i < 5; i++) {
        fiveStatus[i] = j["fiveStatus"][i];
        fiveStatusLimit[i] = j["fiveStatusLimit"][i];
    }
    
    skillPt = j["skillPt"];
    skillScore = j["skillScore"];
    for (int i = 0; i < 5; i++) {
      trainLevelCount[i] = j["trainLevelCount"][i];
    }

    failureRateBias= j["failureRateBias"];
    isQieZhe = j["isQieZhe"];
    isAiJiao = j["isAiJiao"];
    isPositiveThinking = j["isPositiveThinking"];
    isRefreshMind = j["isRefreshMind"];

    haveCatchedDoll = j.contains("haveCatchedDoll") ? bool(j["haveCatchedDoll"]) : false;

    stage = j["stage"];
    decidingEvent = j["decidingEvent"];
    isRacing = j["isRacing"];
    if (isRacing != isRacingTurn[turn])
    {
      cout << "Warning:实际赛程和预期赛程不一致" << endl;
      isRacingTurn[turn] = isRacing;
    }
    for (int i = 0; i < 6; i++) {
      persons[i].friendship = j["persons"][i]["friendship"];
      persons[i].isHint = j["persons"][i]["isHint"];
    }
    friendship_noncard_yayoi = j["friendship_noncard_yayoi"];
    friendship_noncard_reporter = j["friendship_noncard_reporter"];

    for (int i = 0; i < 5; i++) {
      for (int p = 0; p < 5; p++) {
        int pid = j["personDistribution"][i][p];
        if (pid == 102) {
          personDistribution[i][p] = PS_noncardYayoi;
        }
        else if (pid == 103) {
          personDistribution[i][p] = PS_noncardReporter;
        }
        else if ((pid >= 0 && pid < 6) || (pid >= PS_guest0 && pid < PS_guestEnd))
        {
          personDistribution[i][p] = pid;
        }
        else if (pid == -1)
        {
          personDistribution[i][p] = -1;
        }
        else
        {
          throw "Game::loadGameFromJson读取到未知的personId:" + to_string(pid);
        }
      }
    }
    


    if (friend_type != 0) {
      friend_outgoingNum = j.value("friend_outgoingNum", 0);
      friend_stage = j["friend_stage"];
    }

    //无人岛剧本状态。json格式在第5步（Protocol）再最终确定，这里先读能读到的字段
    if (j.contains("mj_facilityLevel"))
    {
      for (int i = 0; i < 6; i++)
      {
        mj_facilityLevel[i] = j["mj_facilityLevel"][i];
        mj_facilityJukuren[i] = j["mj_facilityJukuren"][i];
      }
    }
    if (j.contains("mj_plan"))
    {
      mj_planNum = 0;
      for (auto& f : j["mj_plan"])
        mj_plan[mj_planNum++] = MujintoFacility(f["type"], f["level"], f.value("jukuren", false));
    }
    //正在制定建设计划时（stage=ST_plan）需要这一期的总格数
    mj_planPending = j.value("mj_planPending", false);
    mj_planSpace = j.value("mj_planSpace", 0);
    if (stage == ST_plan)
      mj_planPending = true;
    mj_pioneerPt = j.value("mj_pioneerPt", 0);
    mj_requiredPt1 = j.value("mj_requiredPt1", 0);
    mj_requiredPt2 = j.value("mj_requiredPt2", 0);
    mj_ticket = j.value("mj_ticket", 0);
    mj_bonusTrainingEffect = j.value("mj_bonusTrainingEffect", 0);
    mj_bonusHint = j.value("mj_bonusHint", 0);
    mj_bonusPioneerPt = j.value("mj_bonusPioneerPt", 0);
    if (j.contains("mj_evalResult"))
      for (int i = 0; i < 6; i++)
        mj_evalResult[i] = j["mj_evalResult"][i];
    if (j.contains("mj_guestType"))
    {
      mj_guestNum = 0;
      for (auto& t : j["mj_guestType"])
        if (mj_guestNum < MJ_MAX_GUEST)
          mj_guestType[mj_guestNum++] = t;
    }
    if (j.contains("mj_deyilvBonus"))
      for (int i = 0; i < 6; i++)
        mj_deyilvBonus[i] = j["mj_deyilvBonus"][i];

    calculateTrainingValue();
    {
      std::mt19937_64 houseRand(114514);
      mj_calculateIslandTraining(&houseRand);
    }
  //for (int k = 1; k < 5; k++) {
   //     cout << trainValue[1][k] << endl;
   // }
    
  }
  catch (const char* e)
  {
    cout << "\x1b[91m读取游戏信息json出错：" << e << "\x1b[0m" << endl;
    //cout << "-- json --" << endl << jsonStr << endl;
    return false;
  }
  catch (string e)
  {
    cout << "\x1b[91m读取游戏信息json出错：" << e << "\x1b[0m" << endl;
    //cout << "-- json --" << endl << jsonStr << endl;
    return false;
  }
  catch (std::exception& e)
  {
    cout << "读取游戏信息json出错：未知错误" << endl << e.what() << endl;
    //cout << "-- json --" << endl << jsonStr << endl;
    return false;
  }
  catch (...)
  {
    cout << "读取游戏信息json出错：未知错误"  << endl;
    return false;
  }

  return true;
}

