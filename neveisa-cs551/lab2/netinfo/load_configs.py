import csv


with open("links.csv") as f:
  links = csv.DictReader(f)
  for l in links:
    print l
