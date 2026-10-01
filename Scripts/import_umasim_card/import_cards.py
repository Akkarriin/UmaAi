"""从 umasim (https://github.com/mee1080/umasim) 的 data/support_card.txt 导入支援卡数值到 db/cardDB.json。

原导出脚本(export_support_card)需要游戏本体的 master.mdb，本脚本作为补充，用于补齐新卡。
只转换各突破等级的数值（cardValue）。固有效果(uniqueEffect)格式需单独核对，本脚本不处理。

用法:
  python3 import_cards.py <support_card.txt> <cardDB.json> --check            # 用已有卡核对字段映射
  python3 import_cards.py <support_card.txt> <cardDB.json> --add 10128 ...     # 导入指定卡（cardId，不含突破位）
"""
import argparse
import json
import sys

# umasim SupportStatus 字段顺序（见 umasim core/.../data/SupportCardLoader.kt）
STATUS_FIELDS = [
    "friend", "motivation",
    "speedBonus", "staminaBonus", "powerBonus", "gutsBonus", "wisdomBonus",
    "training",
    "initialSpeed", "initialStamina", "initialPower", "initialGuts", "initialWisdom",
    "initialRelation", "race", "fan", "hintLevel", "hintFrequency", "specialtyRate",
    "eventRecovery", "eventEffect", "failureRate", "hpCost",
    "skillPtBonus", "wisdomFriendRecovery", "initialSkillPt",
]

# umasim 类型名 -> UmaAi cardType（0速 1耐 2力 3根 4智 5友人 6团队）
TYPE_MAP = {"スピード": 0, "スタミナ": 1, "パワー": 2, "根性": 3, "賢さ": 4, "友人": 5, "グループ": 6}
TYPE_PREFIX = ["[速]", "[耐]", "[力]", "[根]", "[智]", "[友]", "[团]"]


def load_umasim(path):
    cards = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            data = line.rstrip("\n").split("\t")
            if len(data) < 2 + 26:
                continue
            card_id, full_name, chara = int(data[0]), data[1], data[2]
            rarity, talent, max_level, type_name = int(data[3]), int(data[4]), int(data[5]), data[6]
            n = len(STATUS_FIELDS)
            status = dict(zip(STATUS_FIELDS, map(int, data[7:7 + n])))
            # 第二组是固有效果中的常驻数值加成，UmaAi 的 cardDB 直接把它合并进各项数值
            unique = dict(zip(STATUS_FIELDS, map(int, data[7 + n:7 + 2 * n])))
            merged = {k: status[k] + unique[k] for k in STATUS_FIELDS}
            # 与原导出脚本保持一致：友情加成、得意率乘算，失败率/体力消耗下降按 1-(1-a)(1-b) 合并，固有的hint发生率不计入
            def mul(k):
                return round((100 + status[k]) * (100 + unique[k]) / 100 - 100, 2)
            def reduce(k):
                return round(100 - (100 - status[k]) * (100 - unique[k]) / 100, 2)
            merged["friend"] = mul("friend")
            merged["specialtyRate"] = mul("specialtyRate")
            merged["failureRate"] = reduce("failureRate")
            merged["hpCost"] = reduce("hpCost")
            merged["hintFrequency"] = status["hintFrequency"]
            status = merged
            entry = cards.setdefault(card_id, {
                "fullName": full_name, "chara": chara, "rarity": rarity, "type": type_name, "levels": {}})
            entry["levels"][talent] = status
    return cards


def to_card_value(card_type, rarity, s):
    is_friend = card_type >= 5
    # UmaAi 的 hintLevel 在 umasim 为正数时多 1（友人/团队卡固定为 0），hintBonus 的 pt 为 hintLevel*5（至少为 5）
    hint_level = 0 if is_friend or s["hintLevel"] == 0 else s["hintLevel"] + 1
    v = {
        "filled": True,
        "bonus": [s["speedBonus"], s["staminaBonus"], s["powerBonus"], s["gutsBonus"], s["wisdomBonus"], s["skillPtBonus"]],
        "initialBonus": [s["initialSpeed"], s["initialStamina"], s["initialPower"], s["initialGuts"], s["initialWisdom"], s["initialSkillPt"]],
        "hintBonus": [0, 0, 0, 0, 0, max(5, hint_level * 5)],
        "hintLevel": hint_level,
        "youQing": s["friend"],
        "ganJing": s["motivation"],
        "xunLian": s["training"],
        "initialJiBan": s["initialRelation"],
        "saiHou": s["race"],
        "hintProbIncrease": s["hintFrequency"],
        "deYiLv": s["specialtyRate"],
        "eventRecoveryAmountUp": s["eventRecovery"],
        "eventEffectUp": s["eventEffect"],
        "failRateDrop": s["failureRate"],
        "vitalCostDrop": s["hpCost"],
        "wizVitalBonus": s["wisdomFriendRecovery"],
    }
    return v


def tidy(x):
    """整数值的浮点数写成整数，与原有 cardDB 的格式一致"""
    if isinstance(x, float) and x.is_integer():
        return int(x)
    if isinstance(x, list):
        return [tidy(v) for v in x]
    if isinstance(x, dict):
        return {k: tidy(v) for k, v in x.items()}
    return x


def convert(card_id, u):
    card_type = TYPE_MAP[u["type"]]
    values = [to_card_value(card_type, u["rarity"], u["levels"][t]) for t in range(5)]
    title = u["fullName"].split("]")[0] + "]" if u["fullName"].startswith("[") else ""
    return tidy({
        "cardId": card_id,
        "charaId": 0,  # umasim 中没有角色 id，需要时手动填写（用于判断剧本 link）
        "cardName": TYPE_PREFIX[card_type] + u["chara"],
        "fullName": title + u["chara"],
        "rarity": u["rarity"],
        "cardType": card_type,
        "cardValue": values,
    })


def check(umasim, db):
    """对比已有卡，统计每个字段不一致的次数"""
    mismatch = {}
    total = 0
    for key, card in db.items():
        u = umasim.get(card["cardId"])
        if u is None or len(u["levels"]) < 5:
            continue
        conv = convert(card["cardId"], u)
        if conv["cardType"] != card["cardType"]:
            mismatch["cardType"] = mismatch.get("cardType", 0) + 1
        for a, b in zip(card["cardValue"], conv["cardValue"]):
            total += 1
            for k, bv in b.items():
                if a.get(k, 0 if not isinstance(bv, list) else [0] * 6) != bv:
                    mismatch[k] = mismatch.get(k, 0) + 1
    print(f"核对了 {total} 个(卡,突破)组合，不一致字段计数: {mismatch}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("umasim_txt")
    ap.add_argument("card_db")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--add", type=int, nargs="*", default=[])
    args = ap.parse_args()

    umasim = load_umasim(args.umasim_txt)
    with open(args.card_db, encoding="utf-8-sig") as f:
        db = json.load(f)

    if args.check:
        check(umasim, db)
    if args.add:
        for cid in args.add:
            if cid not in umasim:
                sys.exit(f"umasim 数据中没有卡 {cid}")
            db[str(cid)] = convert(cid, umasim[cid])
            print(f"已导入 {cid} {db[str(cid)]['fullName']}")
        with open(args.card_db, "w", encoding="utf-8") as f:
            json.dump(db, f, ensure_ascii=False, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
