<script lang="ts">
	import { Button } from '$lib/components/ui/button';
	import { Checkbox } from '$lib/components/ui/checkbox';
	import * as Dialog from '$lib/components/ui/dialog';
	import { API_UPLOADS } from '$lib/constants';
	import { ChatUploadsService } from '$lib/services/chat-uploads.service';
	import { apiFetch } from '$lib/utils';

	interface FileEntry {
		mtime?: number;
		name: string;
		path: string;
		size: number;
	}

	interface Props {
		open?: boolean;
	}

	type SortKey = 'mtime' | 'name' | 'size';
	type SortDir = 'asc' | 'desc';

	let { open = $bindable(false) }: Props = $props();

	let files = $state<FileEntry[]>([]);
	let folder = $state('~/.llama/uploads');
	let error = $state('');
	let loading = $state(false);
	let selected = $state<Record<string, boolean>>({});
	let deleting = $state(false);
	let sortKey = $state<SortKey>('mtime');
	let sortDir = $state<SortDir>('desc');

	const sortedFiles = $derived.by(() => {
		const list = [...files];
		const dir = sortDir === 'asc' ? 1 : -1;

		list.sort((a, b) => {
			if (sortKey === 'name') {
				return a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' }) * dir;
			}

			if (sortKey === 'size') {
				return (a.size - b.size) * dir;
			}

			return ((a.mtime ?? 0) - (b.mtime ?? 0)) * dir;
		});

		return list;
	});

	const selectedPaths = $derived(
		sortedFiles.filter((file) => selected[file.path]).map((file) => file.path)
	);
	const allSelected = $derived(sortedFiles.length > 0 && selectedPaths.length === sortedFiles.length);
	const someSelected = $derived(selectedPaths.length > 0 && !allSelected);

	function formatSize(size: number): string {
		if (size < 1024) {
			return `${size} B`;
		}
		if (size < 1024 * 1024) {
			return `${(size / 1024).toFixed(1)} KB`;
		}
		return `${(size / (1024 * 1024)).toFixed(1)} MB`;
	}

	function formatMtime(mtime?: number): string {
		if (!mtime) {
			return '—';
		}

		return new Date(mtime * 1000).toLocaleString();
	}

	function sortMark(key: SortKey): string {
		if (sortKey !== key) {
			return '';
		}

		return sortDir === 'desc' ? ' ▼' : ' ▲';
	}

	function clickSort(key: SortKey) {
		if (sortKey === key) {
			sortDir = sortDir === 'desc' ? 'asc' : 'desc';
			return;
		}

		sortKey = key;
		sortDir = 'desc';
	}

	async function load() {
		loading = true;
		error = '';
		selected = {};

		try {
			files = await ChatUploadsService.listFiles();
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to list files';
			files = [];
		} finally {
			loading = false;
		}
	}

	$effect(() => {
		if (open) {
			void load();
		}
	});

	function toggleFile(path: string, checked: boolean) {
		selected = { ...selected, [path]: checked };
	}

	function toggleSelectAll(checked: boolean) {
		const next: Record<string, boolean> = {};
		if (checked) {
			for (const file of sortedFiles) {
				next[file.path] = true;
			}
		}
		selected = next;
	}

	async function deleteSelected() {
		if (selectedPaths.length === 0) {
			return;
		}

		deleting = true;
		error = '';

		try {
			await apiFetch(API_UPLOADS.DELETE_FILES, {
				body: JSON.stringify({ paths: selectedPaths }),
				method: 'POST'
			});
			await load();
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to delete files';
		} finally {
			deleting = false;
		}
	}
</script>

<Dialog.Root bind:open>
	<Dialog.Content class="sm:max-w-2xl">
		<Dialog.Header>
			<Dialog.Title>Attachment files</Dialog.Title>
			<Dialog.Description>
				Files in ~/.llama/uploads. Select files to delete. Click a column header to sort (default:
				modified time, newest first).
			</Dialog.Description>
		</Dialog.Header>

		<p class="break-all font-mono text-xs text-muted-foreground">{folder}</p>

		{#if error}
			<p class="text-sm text-destructive">{error}</p>
		{/if}

		<div class="max-h-80 overflow-auto rounded-md border">
			{#if loading}
				<p class="p-3 text-sm text-muted-foreground">Loading…</p>
			{:else}
				<table class="w-full text-sm">
					<thead class="sticky top-0 bg-muted">
						<tr class="border-b text-left">
							<th class="w-8 px-2 py-2">
								<Checkbox
									aria-label="Select all files"
									checked={allSelected}
									indeterminate={someSelected}
									onCheckedChange={(value) => toggleSelectAll(value === true)}
								/>
							</th>
							<th class="px-2 py-2">
								<button class="font-medium hover:underline" onclick={() => clickSort('name')} type="button">
									Name{sortMark('name')}
								</button>
							</th>
							<th class="px-2 py-2 whitespace-nowrap">
								<button class="font-medium hover:underline" onclick={() => clickSort('size')} type="button">
									Size{sortMark('size')}
								</button>
							</th>
							<th class="px-2 py-2 whitespace-nowrap">
								<button class="font-medium hover:underline" onclick={() => clickSort('mtime')} type="button">
									Modified{sortMark('mtime')}
								</button>
							</th>
						</tr>
					</thead>
					<tbody>
						{#if sortedFiles.length === 0}
							<tr>
								<td class="px-3 py-3 text-muted-foreground" colspan="4">No files</td>
							</tr>
						{:else}
							{#each sortedFiles as file (file.path)}
								<tr class="border-b last:border-0 hover:bg-accent">
									<td class="px-2 py-2">
										<Checkbox
											checked={Boolean(selected[file.path])}
											onCheckedChange={(value) => toggleFile(file.path, value === true)}
										/>
									</td>
									<td class="max-w-[12rem] truncate px-2 py-2" title={file.name}>{file.name}</td>
									<td class="px-2 py-2 whitespace-nowrap text-muted-foreground">{formatSize(file.size)}</td>
									<td class="px-2 py-2 whitespace-nowrap text-muted-foreground">
										{formatMtime(file.mtime)}
									</td>
								</tr>
							{/each}
						{/if}
					</tbody>
				</table>
			{/if}
		</div>

		<Dialog.Footer>
			<Button onclick={() => (open = false)} type="button" variant="ghost">Close</Button>
			<Button
				disabled={selectedPaths.length === 0 || deleting}
				onclick={() => deleteSelected()}
				type="button"
				variant="destructive"
			>
				{deleting ? 'Deleting…' : `Delete selected (${selectedPaths.length})`}
			</Button>
		</Dialog.Footer>
	</Dialog.Content>
</Dialog.Root>
