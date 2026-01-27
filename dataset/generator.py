import pandas as pd
import numpy as np
import random
import re
from locallm import OllamaLm, LmParams, InferenceParams

NO_CAMPAIGNS = 350
random.seed(42)

# Base electronics and appliances categories
BASE_TERMS = [
    "wireless headphones", "smart earbuds", "bluetooth speaker", "gaming headset", "gaming mouse",
    "mechanical keyboard", "4K TV", "LED TV", "OLED TV", "QLED TV", "smart TV", "soundbar",
    "subwoofer", "portable charger", "power bank", "wireless router", "smartwatch", "fitness tracker",
    "smart scale", "smart light bulb", "LED lamp", "desk lamp", "ceiling lamp", "floor lamp",
    "vacuum cleaner", "robot vacuum", "handheld vacuum", "air purifier", "dehumidifier", "humidifier",
    "coffee maker", "espresso machine", "blender", "food processor", "microwave oven", "convection oven",
    "electric kettle", "rice cooker", "slow cooker", "induction cooktop", "toaster", "toaster oven",
    "dishwasher", "washing machine", "dryer", "refrigerator", "mini fridge", "ice maker", "water purifier",
    "smart home hub", "security camera", "video doorbell", "smart lock", "thermostat", "smoke detector",
    "air fryer", "juicer", "humidifier", "electric grill", "electric skillet", "portable heater", "fan",
    "ceiling fan", "sound system", "home theater", "projector", "webcam",
    "tablet", "laptop", "desktop PC", "external hard drive", "SSD", "flash drive", "memory card",
    "printer", "scanner", "gaming console", "game controller", "microphone",
    "studio headphones", "digital camera", "mirrorless camera", "action camera", "camcorder", "tripod",
    "camera lens", "camera bag", "camera filter"
]

# Augment with MODIFIERS to generate ~500 terms
MODIFIERS = [
    "pro", "touch_screen", "USB_C", "smart_home", "ultra", "black", "compact", "eco", "all_in_one",
    "high_speed", "noise_cancelling", "gaming", "professional", "LED", "bluetooth", "4K", "energy_saving", "advanced"
]

terms = set()
while len(terms) < 500:
    base = random.choice(BASE_TERMS)
    mod = base
    while mod in base and mod != "":
        mod = random.choice(MODIFIERS + [""])  # sometimes no modifier
    term = (mod + " " + base).strip()
    terms.add(term)

# Convert to list and show first 20
terms_list = list(terms)
print(terms_list[:20], len(terms_list))

# Generate trending terms and associated campaign's metadata
trending_terms = []
campaign_names = []
campaign_descriptions = []
llm = OllamaLm(LmParams(is_verbose=True))
llm.load_model("gemma3:1b", 8192)
for campaignID in range(NO_CAMPAIGNS):
    num_terms = random.randint(5, 15)  # more terms per campaign
    sampled_terms = random.choices(terms_list, k=num_terms)
    sampled_terms = set(sampled_terms)
    for term in sampled_terms:
        term_wildcard = "%" + term.replace(" ", "%") + "%"
        trending_terms.append([campaignID, term, term_wildcard])
    resp = llm.infer(
        "Give me a realistic campaign name that is associated with this set of trending terms {}. "
        "Only output one term of three to five words please. "
        "Put the main keyword inside **...** for me.".format(list(sampled_terms)),
        InferenceParams(max_tokens=50, stream=False, repeat_penalty=1.0)
    )
    matches = re.findall(r'\*\*(.*?)\*\*', resp["text"])
    if len(matches) > 0:
        campaign_names.append(matches[0].strip())
    else:
        campaign_names.append(resp["text"].strip())
    resp = llm.infer(
        "Write a marketing description for campaign {}. Give me a single sentence only.".format(campaign_names[-1]),
        InferenceParams(max_tokens=150, stream=False, repeat_penalty=1.0)
    )
    # matches = re.findall(r'\*\*(.*?)\*\*', resp["text"])
    campaign_descriptions.append(resp["text"].strip("**").strip())
    print(campaign_names[-1], campaign_descriptions[-1])

# Generate campaign start and end dates
start_date = pd.Timestamp('2013-01-01')
dates = [start_date]
for i in range(NO_CAMPAIGNS - 1):
    delta = pd.Timedelta(days=np.random.randint(7, 21))
    dates.append(dates[-1] + delta)
campaign_start_dates = pd.to_datetime(dates)
campaign_end_dates = campaign_start_dates + pd.to_timedelta(30, unit='D')  # 30-day campaigns

# Two final dataframes
campaign_df = pd.DataFrame({
    "campaignID": range(NO_CAMPAIGNS),
    "campaignName": campaign_names,
    "campaignStartDate": campaign_start_dates,
    "campaignEndDate": campaign_end_dates,
    "campaignDescription": campaign_descriptions
})
trending_df = pd.DataFrame(trending_terms, columns=['campaignID', 'term', 'termWildcard'])

# Save CSV files
campaign_df.to_csv("campaign.csv", index=False)
trending_df.to_csv('trending_term.csv', index=False)

print(len(campaign_df), len(trending_df))
